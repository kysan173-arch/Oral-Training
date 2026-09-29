#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"
#include <iostream>

namespace {
void check(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void denied(F action, const std::string& code) {
  try { action(); } catch (const ApiError& error) {
    check(error.code == code, "expected " + code + ", got " + error.code);
    return;
  }
  throw std::runtime_error("expected rejection: " + code);
}
}

int main() {
  const auto* url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
  if (!url || !*url) return 77;
  try {
    auto pool = std::make_shared<DatabasePool>(url, 6, std::chrono::milliseconds(1000));
    {
      auto c = pool->acquire(); pqxx::work tx(c.get());
      const auto identity = tx.exec("SELECT current_database(), current_schema()")[0];
      const std::string database_name = identity[0].c_str();
      check((database_name.find("test") != std::string::npos ||
             (database_name.size() >= 3 && database_name.substr(database_name.size()-3) == "_ci")) &&
            std::string(identity[1].c_str()).find("audit_fix_") == 0, "dedicated test database/schema required");
      tx.exec("INSERT INTO users(id,role,display_name) VALUES ('audit-admin','admin','Admin'),('audit-learner','learner','Learner')");
      tx.exec("INSERT INTO supervisor_team_members(learner_id,supervisor_id) VALUES('audit-learner','audit-admin')");
      tx.exec("UPDATE scenarios SET is_active=FALSE WHERE id='implant-basic'");
      tx.commit();
    }
    ReliableDatabase db(pool);
    ReliableRoleplayDatabase rp(pool, true);
    PatientInitializationStore patients(pool, true);
    denied([&] { db.createSession("audit-learner", "implant-basic"); }, "SCENARIO_NOT_FOUND");
    denied([&] { rp.createSession("audit-learner", "implant-basic"); }, "SCENARIO_NOT_FOUND");
    denied([&] { db.createSession("audit-learner", "free-roleplay-template"); }, "SCENARIO_NOT_FOUND");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec("UPDATE scenarios SET is_active=TRUE WHERE id='implant-basic'"); tx.commit();
    }
    const auto training = db.createSession("audit-learner", "implant-basic");
    const auto roleplay = rp.createSession("audit-learner", "implant-basic");
    const auto session_id = training["session"]["id"].get<std::string>();
    db.claimUserMessage("audit-learner", session_id, "audit-message", "private learner conversation");
    denied([&] { db.supervisorMemberSession("audit-admin", "audit-learner", session_id); }, "LEARNER_CONTENT_PRIVATE");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec("UPDATE scenarios SET is_active=FALSE WHERE id='implant-basic'"); tx.commit();
    }
    denied([&] { db.restartSession("audit-learner", session_id); }, "SCENARIO_NOT_FOUND");
    denied([&] { rp.restartSession("audit-learner", roleplay["session"]["id"].get<std::string>()); }, "SCENARIO_NOT_FOUND");
    check(db.getSession("audit-learner", session_id)["session"]["status"] == "in_progress", "failed restart must preserve original session");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec("UPDATE scenarios SET is_active=TRUE WHERE id='implant-basic'"); tx.commit();
    }
    rp.createSession("audit-learner", "free-roleplay-template", "患者咨询场景");

    oral_training::knowledge::KnowledgeStore store(pool);
    const json unknown={{"status","unknown"},{"reason","Not recorded"}};
    json payload={{"name","Published service"},{"category","implant"},{"dataOrigin","synthetic"},
      {"price",{{"status","known"},{"type","fixed"},{"currency","CNY"},{"amountMinor",500000},
        {"unit","per_tooth"},{"conditions","test only"},{"validFrom","2020-01-01"},{"validUntil","2099-12-31"}}},
      {"includedItems",json::array()},{"excludedItems",json::array()},
      {"visitDuration",unknown},{"treatmentDuration",unknown},{"followupInterval",unknown},
      {"appointment",unknown},{"professionalTopics",json::array()}, {"scenarioIds",{"implant-basic"}}};
    const auto service=store.createService("audit-admin",payload,"audit");
    const auto sid=service["id"].get<std::string>();
    const auto published=store.publishService("audit-admin",sid,1,"service-publish",
        oral_training::knowledge::contentSha256(payload),"audit");
    const auto revision=published["revision"]["revisionId"].get<std::string>();
    payload["name"]="UNPUBLISHED DRAFT NAME";
    payload["category"]="UNPUBLISHED DRAFT CATEGORY";
    store.saveServiceDraft("audit-admin",sid,1,payload,"audit");
    bool found = false;
    const auto available = store.listAvailableServices();
    for(const auto& item:available["items"]) if(item["id"]==sid) {
      found=true;
      check(item["name"]=="Published service" && item["category"]=="implant", "draft leaked into learner catalog");
    }
    check(found, "valid service must remain available");
    {
      auto c=pool->acquire(); pqxx::read_transaction tx(c.get());
      check(std::string(tx.exec_params("SELECT name FROM clinic_services WHERE id=$1",sid)[0][0].c_str())=="Published service",
            "saving draft changed the published service projection");
    }
    const auto publishKnowledge = [&](const std::string& body, const std::string& from, const std::string& until) {
      json metadata={{"origin","synthetic"},{"verification","unverified"},{"trainingScope","demo"},
        {"applicability","same audit service"},{"sourceTitle",""},{"sourceUrl",nullptr},{"sourceLocator",""},
        {"effectiveFrom",from},{"effectiveUntil",until},{"aliases",json::array()}};
      auto entry=store.createKnowledge("audit-admin","inclusions","service",sid,"服务拍片资料",body,metadata,"audit");
      return store.publishKnowledge("audit-admin",entry["id"].get<std::string>(),1,entry["id"].get<std::string>(),
        oral_training::knowledge::contentSha256(entry),"audit")["revision"]["revisionId"].get<std::string>();
    };
    const auto expired=publishKnowledge("拍片历史资料。","2020-01-01","2020-12-31");
    const auto future=publishKnowledge("拍片未来资料。","2099-01-01","2099-12-31");
    publishKnowledge("本服务包含拍片。","2020-01-01","2099-12-31");
    // Put complementary entries ahead of a conflicting one to exercise top-K.
    for (int i=0;i<7;++i) publishKnowledge("拍片需要医生评估。","2020-01-01","2099-12-31");
    publishKnowledge("本服务不包含拍片。","2020-01-01","2099-12-31");
    const auto new_session=rp.createSession("audit-learner","implant-basic","",sid,"audit-rag");
    const auto context=rp.getRagContext(new_session["session"]["id"].get<std::string>());
    const auto manifest=context["manifest"].get<std::vector<std::string>>();
    check(std::find(manifest.begin(),manifest.end(),expired)==manifest.end(), "expired knowledge in new snapshot");
    check(std::find(manifest.begin(),manifest.end(),future)==manifest.end(), "future knowledge in new snapshot");
    const auto patient_id=patients.create("audit-learner","implant-basic",sid,"audit-patient");
    const auto patient_context=patients.profiles(patient_id)["context"];
    check(patient_context["manifest"]==context["manifest"], "training and roleplay validity filters differ");
    oral_training::rag::RetrievalRequest request;
    request.context_id=context["contextId"].get<std::string>();
    request.current_question="价格多少？是否包含拍片？";
    request.purpose=oral_training::rag::RetrievalPurpose::CustomerReply;
    oral_training::rag::RagRetriever retriever(pool);
    const json bundle=retriever.retrieve(revision,manifest,context["knowledgeAsOf"].get<std::string>(),
      context["manifestHash"].get<std::string>(),request,"demo");
    check(!bundle["facts"].empty(), "valid price disappeared");
    check(!bundle["conflicts"].empty() && bundle["passages"].size()<=6, "real retrieval must carry conflicts before top-K");
    json ids=json::array(); for(const auto& fact:bundle["facts"]) ids.push_back(fact["evidenceId"]);
    for(const auto& passage:bundle["passages"]) ids.push_back(passage["evidenceId"]);
    const auto reply=oral_training::rag::groundedReply({{"evidenceIds",ids}},context,bundle,"audit-trace");
    check(reply["answerStatus"]=="conflicted" && reply["citations"].empty(), "conflicting evidence was asserted as fact");
    // A locked old snapshot stays interpretable at its original date.
    const std::vector<std::string> old_manifest={expired};
    const auto old_hash=oral_training::rag::manifestHash(revision,old_manifest,"demo");
    const json old=retriever.retrieve(revision,old_manifest,"2020-06-01T12:00:00+08:00",old_hash,request,"demo");
    check(!old["passages"].empty(), "old snapshot lost historically valid knowledge");
    const json now=retriever.retrieve(revision,old_manifest,"2026-09-29T12:00:00+08:00",old_hash,request,"demo");
    check(now["passages"].empty(), "retrieval used expired locked knowledge");

    payload["price"]["validUntil"]="2020-12-31";
    const auto expired_service=store.createService("audit-admin",payload,"audit");
    const auto expired_sid=expired_service["id"].get<std::string>();
    const auto expired_revision=store.publishService("audit-admin",expired_sid,1,"expired-service",
      oral_training::knowledge::contentSha256(payload),"audit")["revision"]["revisionId"].get<std::string>();
    denied([&] { rp.createSession("audit-learner","implant-basic","",expired_sid,"expired-rp"); }, "SERVICE_SCENARIO_MISMATCH");
    denied([&] { patients.create("audit-learner","implant-basic",expired_sid,"expired-patient"); }, "SERVICE_SCENARIO_MISMATCH");
    const auto after_expiration = store.listAvailableServices();
    for(const auto& item:after_expiration["items"]) check(item["id"]!=expired_sid, "expired service listed");
    const json expired_bundle=retriever.retrieve(expired_revision,{},"2026-09-29T00:00:00+08:00",
      oral_training::rag::manifestHash(expired_revision,json::array(),"demo"),request,"demo");
    check(expired_bundle["facts"].empty(), "expired price returned as factual evidence");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec_params("UPDATE sessions SET status='completed', evaluation_status='ready', total_score=NULL, finished_at=NOW() WHERE id=$1", session_id);
      const json unscored={{"schemaVersion",2},{"totalScore",nullptr},{"passed",nullptr},
        {"dimensionScores",{{"knowledgeAccuracy",nullptr},{"medicalCompliance",80},{"empathy",80},
                            {"needsDiscovery",80},{"serviceEtiquette",80}}}};
      tx.exec_params("INSERT INTO evaluations(session_id,status,report) VALUES ($1,'ready',$2::jsonb)",session_id,unscored.dump());
      tx.commit();
    }
    const auto member=db.supervisorMemberDetail("audit-admin","audit-learner");
    check(member["averageScore"].is_null() && member["passRate"].is_null(), "unscored member treated as a failure");
    check(member["dimensionAverages"]["knowledgeAccuracy"].is_null(), "missing member dimension became zero");
    check(!member.contains("inspectSessions"), "admin received conversation inspection links");
    store.publishService("audit-admin",sid,2,"service-republish",oral_training::knowledge::contentSha256(payload),"audit");
    check(rp.getSession("audit-learner",new_session["session"]["id"].get<std::string>())["session"]["serviceSummary"]["name"]=="Published service",
          "old session service name must follow its locked revision");
    const auto republished_catalog=store.listAvailableServices();
    for(const auto& item:republished_catalog["items"]) if(item["id"]==sid)
      check(item["name"]=="UNPUBLISHED DRAFT NAME", "publishing must update the learner catalog");
    check(db.healthy(), "fully migrated schema must be healthy");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec("ALTER TABLE training_contexts RENAME COLUMN initialization_status TO audit_missing_column"); tx.commit();
    }
    check(!db.healthy(), "missing mandatory schema must fail readiness");
    {
      auto c=pool->acquire(); pqxx::work tx(c.get());
      tx.exec("ALTER TABLE training_contexts RENAME COLUMN audit_missing_column TO initialization_status"); tx.commit();
    }
    std::cout << "audit regressions passed: privacy, scenarios, draft visibility, validity, real retrieval conflicts\n";
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
