#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <cstdlib>
#include <iostream>

namespace {

constexpr char kLearnerId[] = "feature-test-learner";
constexpr char kPeerId[] = "feature-test-peer";
constexpr char kAdminId[] = "feature-test-admin";
constexpr char kOutsiderId[] = "feature-test-outsider";
constexpr char kReportSessionId[] = "feature-test-report";
constexpr char kEmptyLearnerId[] = "feature-test-empty-learner";
constexpr char kOldReportSessionId[] = "feature-test-old-report";

void cleanupFeatureUsers(const std::string& database_url) {
  pqxx::connection connection(database_url);
  pqxx::work tx(connection);
  for (const std::string user_id : {std::string(kLearnerId), std::string(kPeerId),
                                    std::string(kAdminId), std::string(kOutsiderId),
                                    std::string(kEmptyLearnerId)}) {
    tx.exec_params("DELETE FROM supervisor_team_members WHERE learner_id = $1 OR supervisor_id = $1", user_id);
    tx.exec_params("DELETE FROM auth_sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM learner_checkins WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM roleplay_sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM sessions WHERE user_id = $1", user_id);
    tx.exec_params("DELETE FROM users WHERE id = $1", user_id);
  }
  tx.commit();
}

bool hasMember(const json& members, const std::string& id) {
  for (const auto& member : members) {
    if (member.value("id", "") == id) return true;
  }
  return false;
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
      /* 主管端的聚合口径已收敛到「我的团队」，测试数据必须显式建立归属，
         否则 dashboard / member list 会因团队为空而查不到这两名学员。 */
      tx.exec_params(R"(
        INSERT INTO supervisor_team_members(learner_id, supervisor_id)
        VALUES ($1, $3), ($2, $3)
      )", kLearnerId, kPeerId, kAdminId);
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
    {
      const json scored_report = {
          {"dimensionScores", {{"knowledgeAccuracy", 80}, {"medicalCompliance", 80},
                               {"empathy", 80}, {"needsDiscovery", 80},
                               {"serviceEtiquette", 80}}},
      };
      const json lower_report = {
          {"dimensionScores", {{"knowledgeAccuracy", 60}, {"medicalCompliance", 60},
                               {"empathy", 60}, {"needsDiscovery", 60},
                               {"serviceEtiquette", 60}}},
      };
      const json unscored_v2_report = {
          {"schemaVersion", 2}, {"totalScore", nullptr}, {"passed", nullptr},
          {"dimensionScores", {{"knowledgeAccuracy", nullptr}, {"medicalCompliance", 90},
                               {"empathy", 85}, {"needsDiscovery", 75},
                               {"serviceEtiquette", 95}}},
          {"knowledgeAssessment", {{"status", "insufficient_evidence"},
                                    {"knowledgeAccuracy", nullptr}, {"assessableCount", 0},
                                    {"unassessableCount", 2}, {"coverage", 0}}},
      };
      pqxx::connection connection(database_url);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        INSERT INTO sessions
          (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state,
           started_at, updated_at, finished_at, evaluation_status, total_score)
        VALUES ('feature-mixed-80', $1, 'implant-basic', 'Mixed 80', 'completed', 1, 10,
                  '{}'::jsonb, NOW() - INTERVAL '3 days', NOW(), NOW() - INTERVAL '3 days', 'ready', 80),
               ('feature-mixed-null', $1, 'implant-basic', 'Mixed null', 'completed', 1, 10,
                  '{}'::jsonb, NOW() - INTERVAL '2 days', NOW(), NOW() - INTERVAL '2 days', 'ready', NULL),
               ('feature-mixed-60', $1, 'implant-basic', 'Mixed 60', 'completed', 1, 10,
                  '{}'::jsonb, NOW() - INTERVAL '1 day', NOW(), NOW() - INTERVAL '1 day', 'ready', 60)
      )", kEmptyLearnerId);
      tx.exec_params(R"(
        INSERT INTO evaluations(session_id, status, report, model_version, prompt_version, generated_at)
        VALUES ('feature-mixed-80', 'ready', $1::jsonb, 'fixture', 'fixture', NOW()),
               ('feature-mixed-null', 'ready', $2::jsonb, 'fixture', 'score-rag-v1', NOW()),
               ('feature-mixed-60', 'ready', $3::jsonb, 'fixture', 'fixture', NOW())
      )", scored_report.dump(), unscored_v2_report.dump(), lower_report.dump());
      tx.commit();
    }
    const auto mixed_dashboard = database.dashboard(kEmptyLearnerId, false);
    require(mixed_dashboard["completedSessions"] == 3 && mixed_dashboard["scoredSessions"] == 2 &&
                mixed_dashboard["unscoredSessions"] == 1 && mixed_dashboard["averageScore"] == 70,
            "mixed v1/v2 dashboard did not exclude null total scores");
    require(mixed_dashboard["dimensionAverages"]["knowledgeAccuracy"] == 70 &&
                mixed_dashboard["dimensionAverages"]["medicalCompliance"] == 76.7,
            "dimension averages did not use per-dimension non-null counts");
    const auto mixed_profile = database.learningProfile(kEmptyLearnerId);
    require(mixed_profile["overall"]["totalCompleted"] == 3 &&
                mixed_profile["overall"]["scoredCount"] == 2 &&
                mixed_profile["overall"]["unscoredCount"] == 1 &&
                mixed_profile["overall"]["averageScore"] == 70 &&
                mixed_profile["trend"].size() == 2,
            "learning profile treated an unscored v2 report as zero");
    for (int read = 0; read < 2; ++read) {
      const auto result = database.getEvaluation(kEmptyLearnerId, "feature-mixed-null");
      require(result["status"] == "ready" && result["evaluation"]["totalScore"].is_null(),
              "v2 insufficient-evidence report did not remain ready and unscored");
    }
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      require(tx.exec("SELECT 1 FROM ai_jobs WHERE target_id = 'feature-mixed-null'").empty(),
              "reading a valid unscored v2 report enqueued a model job");
    }

    const auto scenarios = database.listScenarios(kLearnerId);
    require(!scenarios["items"].empty() && scenarios["items"][0].contains("category"),
            "scenario categories were not returned");

    const auto created = database.createSession(kLearnerId, "post-treatment-discomfort");
    const auto active_session_id = created["session"]["id"].get<std::string>();
    /* 提示限额是「总 3 条 + 每轮 1 条」：会话刚开始轮次为 0，还没有可针对的患者发言。
       存储层只认 round 键，round=0 同样进不去，所以「先回复患者才有提示」由服务层
       的 HINT_ROUND_NOT_READY 与这里的事务口径共同保证。下面直接推进轮次测限额，
       不驱动真实模型调用。 */
    constexpr int kHintRounds[] = {1, 2, 3};
    for (int index = 0; index < 3; ++index) {
      const auto round = kHintRounds[index];
      const auto hint = database.requestTrainingHint(
          kLearnerId, active_session_id, round, std::string("feature hint ") + std::to_string(round), 1, 3);
      require(hint["hint"]["number"].get<int>() == index + 1, "training hint number was not incremented");
      require(hint["hint"]["round"].get<int>() == round, "training hint did not record its round");
      require(!hint["hint"]["content"].get<std::string>().empty(), "training hint content was empty");
      require(hint["hintRemaining"].get<int>() == 2 - index, "training hint remaining total was wrong");
      require(hint["hintRemainingThisRound"].get<int>() == 0,
              "training hint did not report itself spent for the round");
      /* 同一轮第二次必须被拒——这是「每轮 1 条」的核心保证。 */
      try {
        (void)database.requestTrainingHint(kLearnerId, active_session_id, round, "second hint", 1, 3);
        throw std::runtime_error("a second hint in the same round was accepted");
      } catch (const ApiError& error) {
        require(error.code == "HINT_ROUND_LIMIT_REACHED", "unexpected second-hint error");
      }
    }
    try {
      (void)database.requestTrainingHint(kLearnerId, active_session_id, 4, "fourth hint", 1, 3);
      throw std::runtime_error("fourth training hint was accepted");
    } catch (const ApiError& error) {
      require(error.code == "HINT_LIMIT_REACHED", "unexpected fourth-hint error");
    }
    const auto hinted_session = database.getSession(kLearnerId, active_session_id);
    require(hinted_session["hints"].size() == 3, "stored training hints were not returned with the session");
    require(hinted_session["hints"][0]["round"].get<int>() == 1,
            "stored training hint did not carry its round");
    require(hinted_session["hintRemaining"].get<int>() == 0, "session did not report the exhausted total");
    /* 会话轮次仍是 0，所以本轮的额度必须是满的：提示额度跟着实际轮次走，不是全局封死。 */
    require(hinted_session["hintRemainingThisRound"].get<int>() == 1,
            "session did not report a fresh per-round hint allowance");

    const auto phrases = database.listLearningPhrases(kLearnerId, "", "", "", false, 20);
    require(!phrases["items"].empty() && phrases["items"][0]["phraseKey"] == "feature-phrase",
            "report-derived phrase was not listed");
    /* 话术的分类必须与场景目录同源，前端筛选才有意义 */
    std::string report_category;
    for (const auto& scenario : scenarios["items"]) {
      if (scenario["id"].get<std::string>() == "implant-basic") {
        report_category = scenario["category"].get<std::string>();
      }
    }
    require(!report_category.empty(), "implant-basic scenario was missing from the catalog");
    require(phrases["items"][0]["category"].get<std::string>() == report_category,
            "listed phrase did not carry its scenario category");
    bool catalog_has_category = false;
    for (const auto& item : phrases["sceneCategories"]) {
      if (item["id"].get<std::string>() == report_category) catalog_has_category = true;
    }
    require(catalog_has_category, "scene category catalog did not include the report category");
    /* 分类筛选在 LIMIT 之前生效：命中分类能查到，其他分类必须为空 */
    const auto filtered = database.listLearningPhrases(kLearnerId, "", "", report_category, false, 20);
    require(filtered["items"].size() == 1 && filtered["items"][0]["phraseKey"] == "feature-phrase",
            "scene category filter dropped the matching phrase");
    const auto other_category = report_category == "consultation" ? "price_negotiation" : "consultation";
    const auto excluded = database.listLearningPhrases(kLearnerId, "", "", other_category, false, 20);
    require(excluded["items"].empty(), "scene category filter leaked phrases from another category");
    try {
      (void)database.listLearningPhrases(kLearnerId, "", "", "not_a_category", false, 20);
      throw std::runtime_error("unknown scene category was accepted");
    } catch (const ApiError& error) {
      require(error.code == "INVALID_ARGUMENT", "unexpected unknown-category error");
    }
    const auto favorite = database.setLearningPhraseFavorite(kLearnerId, kReportSessionId, "feature-phrase", true);
    require(favorite["favorited"].get<bool>(), "phrase favorite was not stored");
    const auto favorites = database.listLearningPhrases(kLearnerId, "", "", "", true, 20);
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

    const auto dashboard = database.supervisorDashboard(kAdminId, "all");
    /* 场景数刻意不写死：迁移 009 新增场景后，「== 4」这种硬编码会静默失效。
       改为与 listScenarios 的实际条数对齐，并额外确认学员练过的场景在列表里。 */
    require(dashboard["studentCount"].get<int>() >= 2,
            "supervisor aggregate did not include learner data");
    require(dashboard["scenarioStats"].size() == scenarios["items"].size(),
            "supervisor aggregate did not cover every scenario");
    require(!dashboard.contains("members") && !dashboard.contains("recentSessions"),
            "supervisor aggregate leaked member-level data");
    {
      bool found_trained_scenario = false;
      for (const auto& stat : dashboard["scenarioStats"]) {
        if (stat.value("scenarioId", "") == "implant-basic") found_trained_scenario = true;
      }
      require(found_trained_scenario, "supervisor aggregate omitted the trained scenario");
    }
    const auto members = database.listSupervisorMembers(kAdminId, 100);
    require(hasMember(members["members"], kLearnerId) && hasMember(members["members"], kPeerId),
            "supervisor member list omitted test learners");
    const auto member = database.supervisorMemberDetail(kAdminId, kLearnerId);
    require(member["member"]["id"] == kLearnerId && member["trend"].size() >= 1 &&
            member["dimensionAverages"].contains("medicalCompliance"),
            "supervisor member detail was incomplete");

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
    const auto weekly_dashboard = database.supervisorDashboard(kAdminId, "week");
    require(weekly_dashboard["completedSessions"].get<int>() == expected_weekly_completed,
            "supervisor week range used update time instead of completion time");

    /* ── 团队归属：候选人 → 加入 → 移出 ───────────────────────────────────
       用第三名学员串一遍完整流程，避免动到上面聚合断言依赖的归属关系。 */
    {
      pqxx::connection connection(database_url);
      pqxx::work tx(connection);
      tx.exec_params(R"(
        INSERT INTO users(id, display_name, role, status, is_demo)
        VALUES ($1, 'Feature Outsider', 'learner', 'active', TRUE)
        ON CONFLICT (id) DO NOTHING
      )", kOutsiderId);
      tx.commit();
    }
    auto candidates = database.listTeamCandidates(kAdminId, 100);
    require(hasMember(candidates["candidates"], kOutsiderId),
            "unassigned learner was not offered as a team candidate");
    require(!hasMember(candidates["candidates"], kLearnerId),
            "learner already in the team leaked into the candidate list");

    const auto added = database.addTeamMembers(kAdminId, {kOutsiderId});
    require(added["addedCount"].get<int>() == 1, "team member was not added");

    /* 重复添加已归属学员：全部落空时必须明确失败，而不是回一个 addedCount=0 的
       「成功」——前端把 0 当成功会显示「已添加 0 人」，掩盖真正的原因。 */
    try {
      (void)database.addTeamMembers(kAdminId, {kOutsiderId});
      throw std::runtime_error("adding an already-assigned member was accepted");
    } catch (const ApiError& error) {
      require(error.code == "TEAM_MEMBER_UNAVAILABLE", "unexpected re-add error");
    }

    /* 已加入团队的学员不应再出现在候选名单里（一人一主管）。 */
    candidates = database.listTeamCandidates(kAdminId, 100);
    require(!hasMember(candidates["candidates"], kOutsiderId),
            "newly added member was still offered as a candidate");

    const auto teammate = database.supervisorMemberDetail(kAdminId, kOutsiderId);
    require(teammate["member"]["id"] == kOutsiderId,
            "newly added member was not readable through the supervisor detail route");

    /* 不能移出别人的成员：换一个主管身份操作必须 404。 */
    try {
      (void)database.removeTeamMember("feature-test-nobody", kOutsiderId);
      throw std::runtime_error("removing a member through a foreign supervisor succeeded");
    } catch (const ApiError& error) {
      require(error.code == "TEAM_MEMBER_NOT_FOUND", "unexpected foreign-owner removal error");
    }

    const auto removed = database.removeTeamMember(kAdminId, kOutsiderId);
    require(removed["removed"].get<bool>(), "team member was not removed");
    try {
      (void)database.supervisorMemberDetail(kAdminId, kOutsiderId);
      throw std::runtime_error("removed member was still readable by the supervisor");
    } catch (const ApiError& error) {
      require(error.code == "MEMBER_NOT_FOUND", "unexpected error after removing a member");
    }

    /* 移出后应重新变回候选人（账号与数据都保留，可再次加入）。 */
    candidates = database.listTeamCandidates(kAdminId, 100);
    require(hasMember(candidates["candidates"], kOutsiderId),
            "removed member was not restored to the candidate pool");

    /* 部分成功：本次带一个已在团队里的人 + 一个候选人，只应加入后者并回报跳过 1 人。 */
    const auto partial = database.addTeamMembers(kAdminId, {kLearnerId, kOutsiderId});
    require(partial["addedCount"].get<int>() == 1 && partial["skippedCount"].get<int>() == 1,
            "partial team add did not report added/skipped counts");

    /* 移出团队只解除归属，绝不能连带删除训练数据；移出后也应能重新加入。 */
    (void)database.removeTeamMember(kAdminId, kLearnerId);
    {
      pqxx::connection connection(database_url);
      pqxx::read_transaction tx(connection);
      const auto remaining = tx.exec_params(
          "SELECT COUNT(*) AS count FROM sessions WHERE user_id = $1",
          kLearnerId)[0]["count"].as<int>();
      require(remaining >= 1, "removing a member from the team deleted their training sessions");
    }
    const auto restored = database.addTeamMembers(kAdminId, {kLearnerId});
    require(restored["addedCount"].get<int>() == 1, "removed member could not be re-added");

    // Master: 200-session paging + trend, old-report favorite/mistake paging, legacy report handling.
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
    const auto paged_favorites = database.listLearningPhrases(kLearnerId, "", "", "", true, 20);
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
