#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <cstdlib>
#include <iostream>

namespace {

constexpr char kLearnerId[] = "feature-test-learner";
constexpr char kPeerId[] = "feature-test-peer";
constexpr char kEmptyLearnerId[] = "feature-test-empty-learner";
constexpr char kAdminId[] = "feature-test-admin";
constexpr char kReportSessionId[] = "feature-test-report";
constexpr char kOldReportSessionId[] = "feature-test-old-report";

void cleanupFeatureUsers(const std::string& database_url) {
  pqxx::connection connection(database_url);
  pqxx::work tx(connection);
  for (const std::string user_id : {std::string(kLearnerId), std::string(kPeerId),
                                    std::string(kEmptyLearnerId), std::string(kAdminId)}) {
    tx.exec_params("DELETE FROM auth_sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM learner_checkins WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM roleplay_sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM users WHERE id = $1", user_id);
  }
  tx.commit();
}

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

#include "job_lock_order_test.h"

int main() {
  const char* raw_url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
  if (raw_url == nullptr || std::string(raw_url).empty()) {
    std::cout << "database feature test skipped: ORAL_TRAINING_TEST_DATABASE_URL is not set\n";
    return 77;
  }
  const std::string database_url(raw_url);
  auto lower_url = database_url;
  std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (lower_url.find("test") == std::string::npos && lower_url.find("ci") == std::string::npos) {
    std::cerr << "database feature test refuses non-test database\n";
    return 1;
  }

  try {
    cleanupFeatureUsers(database_url);
    {
      pqxx::connection connection(database_url);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        INSERT INTO users(id, display_name, role, status, is_demo)
        VALUES ($1, 'Feature Learner', 'learner', 'active', TRUE),
               ($2, 'Feature Peer', 'learner', 'active', TRUE),
               ($3, 'Feature Supervisor', 'admin', 'active', TRUE),
               ($4, 'Feature Empty Learner', 'learner', 'active', TRUE)
      )", kLearnerId, kPeerId, kAdminId, kEmptyLearnerId);
      const json report = {
          {"dimensionScores", {{"knowledgeAccuracy", 82}, {"medicalCompliance", 88}, {"empathy", 78},
                               {"needsDiscovery", 74}, {"serviceEtiquette", 86}}},
          {"recommendedPhrases", json::array({{{"phraseKey", "feature-phrase"}, {"round", 1},
              {"patientSays", "I am concerned about the process."},
              {"csReply", "I understand your concern. A doctor will assess the details after examination."},
              {"reason", "Keeps the clinical assessment boundary clear."}}})},
          {"learningMistakes", json::array({{{"mistakeKey", "old-mistake"}, {"kind", "expression"},
              {"priority", "medium"}, {"round", 1}, {"originalQuote", "old wording"},
              {"reason", "Needs a clearer boundary."}, {"recommendedRewrite", "Use a compliant boundary."}}})},
      };
      tx.exec_params(R"(
        INSERT INTO sessions
          (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state,
           started_at, updated_at, finished_at, evaluation_status, total_score)
        VALUES ($1, $2, 'implant-basic', 'Seed report', 'completed', 1, 10, '{}'::jsonb,
                NOW() - INTERVAL '2 days', NOW() - INTERVAL '2 days', NOW() - INTERVAL '2 days', 'ready', 82),
               ('feature-test-peer-report', $3, 'price-comparison', 'Peer report', 'completed', 1, 10, '{}'::jsonb,
                NOW() - INTERVAL '1 day', NOW() - INTERVAL '1 day', NOW() - INTERVAL '1 day', 'ready', 68),
               ($4, $2, 'implant-basic', 'Old report', 'completed', 1, 10, '{}'::jsonb,
                NOW() - INTERVAL '400 days', NOW(), NOW() - INTERVAL '400 days', 'ready', 64)
      )", kReportSessionId, kLearnerId, kPeerId, kOldReportSessionId);
      tx.exec_params(R"(
        INSERT INTO evaluations(session_id, status, report, model_version, prompt_version, generated_at)
        VALUES ($1, 'ready', $2::jsonb, 'feature-test', 'feature-test', NOW()),
               ('feature-test-peer-report', 'ready', $2::jsonb, 'feature-test', 'feature-test', NOW()),
               ($3, 'ready', $2::jsonb, 'feature-test', 'feature-test', NOW())
      )", kReportSessionId, report.dump(), kOldReportSessionId);
      tx.commit();
    }

    const auto database_pool = std::make_shared<DatabasePool>(
        database_url, 4, std::chrono::milliseconds(3000));
    ReliableDatabase database(database_pool);
    require(database.healthy(), "database health did not include the new feature tables");
    const auto empty_profile = database.learningProfile(kEmptyLearnerId);
    require(empty_profile["weaknesses"].empty(),
            "empty learning profile fabricated zero-score weaknesses");
    const auto scenarios = database.listScenarios(kLearnerId);
    require(!scenarios["items"].empty() && scenarios["items"][0].contains("category"),
            "scenario categories were not returned");

    const auto created = database.createSession(kLearnerId, "post-treatment-discomfort");
    const auto active_session_id = created["session"]["id"].get<std::string>();
    for (int hint_number = 1; hint_number <= 3; ++hint_number) {
      const auto hint = database.requestTrainingHint(kLearnerId, active_session_id);
      require(hint["hint"]["number"].get<int>() == hint_number, "training hint number was not incremented");
      require(!hint["hint"]["content"].get<std::string>().empty(), "training hint content was empty");
    }
    try {
      (void)database.requestTrainingHint(kLearnerId, active_session_id);
      throw std::runtime_error("fourth training hint was accepted");
    } catch (const ApiError& error) {
      require(error.code == "HINT_LIMIT_REACHED", "unexpected fourth-hint error");
    }
    require(database.getSession(kLearnerId, active_session_id)["hints"].size() == 3,
            "stored training hints were not returned with the session");

    const auto phrases = database.listLearningPhrases(kLearnerId, "", "", false, 20);
    require(!phrases["items"].empty() && phrases["items"][0]["phraseKey"] == "feature-phrase",
            "report-derived phrase was not listed");
    const auto favorite = database.setLearningPhraseFavorite(kLearnerId, kReportSessionId, "feature-phrase", true);
    require(favorite["favorited"].get<bool>(), "phrase favorite was not stored");
    const auto favorites = database.listLearningPhrases(kLearnerId, "", "", true, 20);
    require(favorites["items"].size() == 1 && favorites["items"][0]["favorited"].get<bool>(),
            "favorite-only phrase query was not user-scoped");

    const auto first_checkin = database.checkIn(kLearnerId);
    const auto second_checkin = database.checkIn(kLearnerId);
    require(first_checkin["checkedIn"].get<bool>() && first_checkin["pointsAwarded"].get<int>() == 10,
            "first daily check-in did not grant ten points");
    require(second_checkin["alreadyCheckedIn"].get<bool>() && second_checkin["pointsAwarded"].get<int>() == 0,
            "duplicate daily check-in granted points");
    const auto mine = database.learningMine(kLearnerId);
    require(mine["points"].get<int>() == 10 && mine["favoritesCount"].get<int>() == 1,
            "mine dashboard did not return the only points source and favorite count");

    const auto dashboard = database.supervisorDashboard("all");
    require(dashboard["studentCount"].get<int>() >= 3 && dashboard["scenarioStats"].size() == 4,
            "supervisor aggregate did not include learner and scenario data");
    require(!dashboard.contains("members") && !dashboard.contains("recentSessions"),
            "supervisor aggregate leaked member-level data");

    int expected_weekly_completed = 0;
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      expected_weekly_completed = tx.exec(R"(
        SELECT COUNT(*) AS count FROM sessions s
        WHERE s.status = 'completed' AND s.evaluation_status = 'ready'
          AND s.finished_at >= (
            date_trunc('week', NOW() AT TIME ZONE 'Asia/Shanghai') AT TIME ZONE 'Asia/Shanghai'
          )
      )")[0]["count"].as<int>();
    }
    const auto weekly_dashboard = database.supervisorDashboard("week");
    require(weekly_dashboard["completedSessions"].get<int>() == expected_weekly_completed,
            "supervisor week range used update time instead of completion time");

    {
      pqxx::connection connection(database_url);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        INSERT INTO sessions
          (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state,
           started_at, updated_at, finished_at, evaluation_status, total_score)
        SELECT 'feature-test-history-' || series.n, $1, 'implant-basic', 'History report',
          'completed', 1, 10, '{}'::jsonb,
          NOW() - ((206 - series.n)::text || ' days')::interval,
          NOW() - ((206 - series.n)::text || ' days')::interval,
          NOW() - ((206 - series.n)::text || ' days')::interval,
          'ready', CASE WHEN series.n = 205 THEN 99 ELSE 70 END
        FROM generate_series(1, 205) AS series(n)
      )", kLearnerId);
      tx.exec(R"(
        INSERT INTO evaluations(session_id, status, report, model_version, prompt_version, generated_at)
        SELECT 'feature-test-history-' || series.n, 'ready',
          jsonb_build_object(
            'dimensionScores', jsonb_build_object(
              'knowledgeAccuracy', 70, 'medicalCompliance', 70, 'empathy', 70,
              'needsDiscovery', 70, 'serviceEtiquette', 70),
            'recommendedPhrases', '[]'::jsonb,
            'learningMistakes', jsonb_build_array(jsonb_build_object(
              'mistakeKey', 'history-mistake', 'kind', 'expression', 'priority', 'medium',
              'round', 1, 'originalQuote', 'history wording', 'reason', 'history reason',
              'recommendedRewrite', 'history rewrite'))),
          'feature-test', 'feature-test', NOW()
        FROM generate_series(1, 205) AS series(n)
      )");
      tx.exec_params(R"(
        INSERT INTO learner_mistake_progress(user_id, session_id, mistake_key, mastered_at, updated_at)
        SELECT $1, 'feature-test-history-' || series.n, 'history-mistake', NOW(), NOW()
        FROM generate_series(1, 205) AS series(n)
      )", kLearnerId);
      tx.commit();
    }

    const auto recent_profile = database.learningProfile(kLearnerId);
    require(recent_profile["overall"]["totalCompleted"].get<int>() == 200 &&
                recent_profile["trend"].back()["totalScore"].get<int>() == 99,
            "learning profile did not use the most recent 200 completed sessions");

    const auto old_favorite = database.setLearningPhraseFavorite(
        kLearnerId, kOldReportSessionId, "feature-phrase", true);
    require(old_favorite["favorited"].get<bool>(), "old report phrase favorite was not stored");
    const auto paged_favorites = database.listLearningPhrases(kLearnerId, "", "", true, 20);
    bool found_old_favorite = false;
    for (const auto& item : paged_favorites["items"]) {
      if (jsonString(item, "sessionId") == kOldReportSessionId) found_old_favorite = true;
    }
    require(found_old_favorite, "favorite phrase beyond the first 200 reports was omitted");

    const auto unmastered_mistakes = database.listLearningMistakes(kLearnerId, "", false, 20);
    bool found_old_mistake = false;
    for (const auto& item : unmastered_mistakes["items"]) {
      if (jsonString(item, "sessionId") == kOldReportSessionId) found_old_mistake = true;
    }
    require(found_old_mistake, "unmastered mistake beyond the first 200 reports was omitted");

    // Legacy reports must keep all their fields and never create model jobs.
    json original_report;
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      original_report = json::parse(tx.exec_params(
          "SELECT report FROM evaluations WHERE session_id = $1", kReportSessionId)[0]["report"].c_str());
    }
    for (const int stored_total : {82, 0, -1}) {
      {
        pqxx::connection connection(database_url);
        pqxx::work tx(connection);
        tx.exec_params("UPDATE evaluations SET report = $2::jsonb WHERE session_id = $1",
                       kReportSessionId, original_report.dump());
        tx.exec_params("UPDATE sessions SET total_score = NULLIF($2::int, -1) WHERE id = $1",
                       kReportSessionId, stored_total);
        tx.commit();
      }
      const int expected = stored_total < 0 ? 82 : stored_total;
      const auto result = database.getEvaluation(kLearnerId, kReportSessionId);
      require(result["status"] == "ready" && result["evaluation"]["totalScore"] == expected,
              "legacy total was not recovered from session or complete dimensions");
      (void)database.finish(kLearnerId, kReportSessionId);
      (void)database.getEvaluation(kLearnerId, kReportSessionId);
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      auto persisted = json::parse(tx.exec_params(
          "SELECT report FROM evaluations WHERE session_id = $1", kReportSessionId)[0]["report"].c_str());
      persisted.erase("totalScore");
      require(persisted == original_report, "legacy report contents were modified");
      require(tx.exec_params("SELECT 1 FROM ai_jobs WHERE target_id = $1", kReportSessionId).empty(),
              "reading a legacy report enqueued a model job");
    }
    auto incomplete = original_report;
    incomplete["dimensionScores"].erase("empathy");
    {
      pqxx::connection connection(database_url);
      pqxx::work tx(connection);
      tx.exec_params("UPDATE evaluations SET report = $2::jsonb WHERE session_id = $1",
                     kReportSessionId, incomplete.dump());
      tx.exec_params("UPDATE sessions SET total_score = NULL WHERE id = $1", kReportSessionId);
      tx.commit();
    }
    try {
      (void)database.getEvaluation(kLearnerId, kReportSessionId);
      throw std::runtime_error("incomplete dimensions were silently scored");
    } catch (const ApiError& error) {
      require(error.code == "REPORT_INVALID", "unexpected legacy report error");
    }
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      require(json::parse(tx.exec_params("SELECT report FROM evaluations WHERE session_id = $1",
          kReportSessionId)[0]["report"].c_str()) == incomplete, "unrecoverable report was destroyed");
      require(tx.exec_params("SELECT 1 FROM ai_jobs WHERE target_id = $1", kReportSessionId).empty(),
              "unrecoverable report was automatically re-enqueued");
    }

    runJobLockOrderTests(database_url);

    cleanupFeatureUsers(database_url);
    std::cout << "database feature tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    try { cleanupFeatureUsers(database_url); } catch (...) {}
    std::cerr << "database feature test failed: " << error.what() << '\n';
    return 1;
  }
}
