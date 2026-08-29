#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <cstdlib>
#include <iostream>

namespace {

constexpr char kLearnerId[] = "feature-test-learner";
constexpr char kPeerId[] = "feature-test-peer";
constexpr char kAdminId[] = "feature-test-admin";
constexpr char kReportSessionId[] = "feature-test-report";

void cleanupFeatureUsers(const std::string& database_url) {
  pqxx::connection connection(database_url);
  pqxx::work tx(connection);
  for (const std::string user_id : {std::string(kLearnerId), std::string(kPeerId), std::string(kAdminId)}) {
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
               ($3, 'Feature Supervisor', 'admin', 'active', TRUE)
      )", kLearnerId, kPeerId, kAdminId);
      const json report = {
          {"dimensionScores", {{"knowledgeAccuracy", 82}, {"medicalCompliance", 88}, {"empathy", 78},
                               {"needsDiscovery", 74}, {"serviceEtiquette", 86}}},
          {"recommendedPhrases", json::array({{{"phraseKey", "feature-phrase"}, {"round", 1},
              {"patientSays", "I am concerned about the process."},
              {"csReply", "I understand your concern. A doctor will assess the details after examination."},
              {"reason", "Keeps the clinical assessment boundary clear."}}})},
      };
      tx.exec_params(R"(
        INSERT INTO sessions
          (id, user_id, scenario_id, scenario_name, status, current_round, max_rounds, patient_state,
           started_at, updated_at, finished_at, evaluation_status, total_score)
        VALUES ($1, $2, 'implant-basic', 'Seed report', 'completed', 1, 10, '{}'::jsonb,
                NOW() - INTERVAL '2 days', NOW() - INTERVAL '2 days', NOW() - INTERVAL '2 days', 'ready', 82),
               ('feature-test-peer-report', $3, 'price-comparison', 'Peer report', 'completed', 1, 10, '{}'::jsonb,
                NOW() - INTERVAL '1 day', NOW() - INTERVAL '1 day', NOW() - INTERVAL '1 day', 'ready', 68)
      )", kReportSessionId, kLearnerId, kPeerId);
      tx.exec_params(R"(
        INSERT INTO evaluations(session_id, status, report, model_version, prompt_version, generated_at)
        VALUES ($1, 'ready', $2::jsonb, 'feature-test', 'feature-test', NOW()),
               ('feature-test-peer-report', 'ready', $2::jsonb, 'feature-test', 'feature-test', NOW())
      )", kReportSessionId, report.dump());
      tx.commit();
    }

    ReliableDatabase database(database_url);
    require(database.healthy(), "database health did not include the new feature tables");
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
    require(dashboard["studentCount"].get<int>() >= 2 && dashboard["scenarioStats"].size() == 4,
            "supervisor aggregate did not include learner and scenario data");
    const auto members = database.listSupervisorMembers(100);
    require(hasMember(members["members"], kLearnerId) && hasMember(members["members"], kPeerId),
            "supervisor member list omitted test learners");
    const auto member = database.supervisorMemberDetail(kLearnerId);
    require(member["member"]["id"] == kLearnerId && member["trend"].size() >= 1 &&
            member["dimensionAverages"].contains("medicalCompliance"),
            "supervisor member detail was incomplete");

    cleanupFeatureUsers(database_url);
    std::cout << "database feature tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    try { cleanupFeatureUsers(database_url); } catch (...) {}
    std::cerr << "database feature test failed: " << error.what() << '\n';
    return 1;
  }
}
