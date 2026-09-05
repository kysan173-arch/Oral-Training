#pragma once

#include <future>

// Runs only inside database_feature_test's explicitly selected disposable DB.
// A held session lock represents the first statement of result polling. Once
// the writer is waiting, NOWAIT probes prove it has not taken the report/job
// locks needed by that poll. No timing-dependent deadlock is required to fail.
void runJobLockOrderTests(const std::string& database_url) {
  for (const std::string type : {"evaluation", "roleplay_summary"}) {
    for (const std::string action : {"save", "fail", "expire"}) {
      const bool evaluation = type == "evaluation";
      const std::string sessions = evaluation ? "sessions" : "roleplay_sessions";
      const std::string reports = evaluation ? "evaluations" : "roleplay_summaries";
      const auto id = makeId("lock_test");
      const auto job_id = "job_" + id;
      const auto application = "lock_" + action + "_" + id;
      const auto worker_url = database_url + (database_url.find('?') == std::string::npos ? "?" : "&") +
          "application_name=" + application + "&options=-c%20statement_timeout%3D10000";
      pqxx::connection control(database_url);
      const auto cleanup = [&] {
        pqxx::work tx(control);
        tx.exec_params("DELETE FROM ai_jobs WHERE id = $1", job_id);
        tx.exec_params("DELETE FROM " + sessions + " WHERE id = $1", id);
        tx.commit();
      };
      try {
        {
          pqxx::work tx(control);
          tx.exec_params("INSERT INTO " + sessions +
              " (id,user_id,scenario_id,scenario_name,status,current_round,max_rounds,finished_at" +
              (evaluation ? ",patient_state) " : ") ") +
              "VALUES ($1,$2,'implant-basic','Lock regression','completed',1,10,NOW()" +
              (evaluation ? ",'{}'::jsonb)" : ")"), id, kLearnerId);
          tx.exec_params("INSERT INTO " + reports + " (session_id,status) VALUES ($1,'generating')", id);
          if (evaluation) tx.exec_params(
              "UPDATE sessions SET evaluation_status = 'generating' WHERE id = $1", id);
          tx.exec_params(R"(
            INSERT INTO ai_jobs(id,job_type,target_id,dedupe_key,status,attempts,max_attempts,lease_until)
            VALUES ($1,$2,$3,$4,'running',3,3,
              NOW() + CASE WHEN $5 THEN INTERVAL '-1 minute' ELSE INTERVAL '5 minutes' END)
          )", job_id, type, id, (evaluation ? "evaluation:" : "roleplay-summary:") + id, action == "expire");
          tx.exec_params(R"(
            INSERT INTO ai_job_attempts(job_id,generation,attempt_number,status)
            VALUES ($1,1,3,'running')
          )", job_id);
          tx.commit();
        }
        auto pool = std::make_shared<DatabasePool>(worker_url, 2, std::chrono::milliseconds(3000));
        ReliableDatabase database(pool);
        ReliableRoleplayDatabase roleplay(pool);
        AiJobQueue queue(pool);
        const AiJob job{job_id, type, id, 1, 3};
        std::future<void> writer; // blocker is destroyed before future on failure
        {
          pqxx::work blocker(control);
          blocker.exec_params("SELECT id FROM " + sessions + " WHERE id = $1 FOR UPDATE", id);
          writer = std::async(std::launch::async, [&] {
            if (action == "expire") { (void)queue.claim("lock-test-worker"); return; }
            if (action == "fail") { queue.fail(job, "TEST_FAILURE", "regression", false); return; }
            if (evaluation) {
              database.saveEvaluation(job, {{"dimensionScores", {{"knowledgeAccuracy",80},
                  {"medicalCompliance",80}, {"empathy",80}, {"needsDiscovery",80},
                  {"serviceEtiquette",80}}}}, "test");
            } else {
              roleplay.saveSummary(job, {{"summary", "lock regression"}}, "test");
            }
          });
          if (action == "expire") {
            require(writer.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                    "lease reaper blocked on a busy session");
            writer.get();
          } else {
            pqxx::connection observer(database_url);
            bool waiting = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
              {
                pqxx::read_transaction tx(observer);
                waiting = tx.exec_params(
                    "SELECT 1 FROM pg_stat_activity WHERE application_name = $1 AND wait_event_type = 'Lock'",
                    application).size() == 1;
              }
              if (waiting || writer.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) break;
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(waiting, "writer did not wait for the session lock");
          }
          blocker.exec_params("SELECT session_id FROM " + reports +
              " WHERE session_id = $1 FOR UPDATE NOWAIT", id);
          const auto state = blocker.exec_params(
              "SELECT status FROM ai_jobs WHERE id = $1 FOR UPDATE NOWAIT", job_id);
          require(std::string(state[0]["status"].c_str()) == "running", "busy target was mutated");
          blocker.commit();
        }
        if (writer.valid()) writer.get();
        if (action == "expire") (void)queue.claim("lock-test-worker");
        {
          pqxx::read_transaction tx(control);
          const auto job_state = tx.exec_params("SELECT status FROM ai_jobs WHERE id = $1", job_id);
          const auto report_state = tx.exec_params("SELECT status FROM " + reports + " WHERE session_id = $1", id);
          require(std::string(job_state[0]["status"].c_str()) == (action == "save" ? "succeeded" : "dead"),
                  "job did not reach expected terminal state");
          require(std::string(report_state[0]["status"].c_str()) == (action == "save" ? "ready" : "failed"),
                  "report did not reach expected terminal state");
        }
        // Check the real polling entry point after the concurrent operation.
        const auto result = evaluation ? database.getEvaluation(kLearnerId, id)
                                       : roleplay.getSummary(kLearnerId, id);
        require(result["status"] == (action == "save" ? "ready" : "failed"),
                "polling did not observe the committed result");
        cleanup();
      } catch (...) {
        cleanup();
        throw;
      }
    }
  }
}
