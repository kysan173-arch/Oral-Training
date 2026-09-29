#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"
#include <future>
#include <iostream>

namespace {
std::map<std::string,int> rejectedReads;
void requireInit(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
template<class F> void expectInitError(F action, const std::string& code) {
  try { action(); } catch (const ApiError& error) {
    requireInit(error.code == code, "expected " + code + ", got " + error.code);
    ++rejectedReads[code]; return;
  }
  throw std::runtime_error("missing error " + code);
}
json initOutput() {
  return {{"publicProfile", {{"displayName","Test patient"}, {"ageRange","30-39"},
      {"initialEmotion","hesitant"}, {"budget","must not leak"}}},
      {"privateProfile", {{"budget","private-budget"}, {"hiddenInformation",json::array({"private-history"})}}},
      {"patientState", {{"emotion","hesitant"}, {"emotionLevel",0}, {"trustLevel",50},
        {"revealedInformation",json::array()}, {"riskTriggered",false}}},
      {"opening","I would like to ask about this service."}};
}
class InitGateway final : public oral_training::IModelGateway {
 public:
  bool configured() const override { return true; }
  std::string modelVersion() const override { return "offline-init-fixture"; }
  void setRuntimeKey(const std::string&) override {}
  json patientReply(const json&,const json&,const json&) const override { throw std::runtime_error("unexpected model call"); }
  json evaluate(const json&,const json&) const override { throw std::runtime_error("unexpected model call"); }
  json evaluateCommunication(const json& history,const json& assessment) const override {
    requireInit(assessment.contains("knowledgeManifestHash"),"communication missing locked knowledge");
    std::string quote;
    for(const auto& message:history) if(message["role"]=="user") quote=message["content"].get<std::string>();
    return {{"dimensionScores",{{"medicalCompliance",80},{"empathy",80},{"needsDiscovery",80},{"serviceEtiquette",80}}},
        {"summary","Fixture communication report"},
        {"strengths",json::array({{{"round",1},{"evidence",quote},{"content","Clear expression"}}})},
        {"improvements",json::array({{{"round",1},{"content","Ask about concerns"}}})},
        {"violations",json::array()},
        {"roundComments",json::array({{{"round",1},{"comment","Listen first"},
          {"recommendedRewrite","您好，我理解您的担忧，请问您最关注哪些方面？"}}})}};
  }
  json standardServiceReply(const json&,const json&) const override { throw std::runtime_error("unexpected model call"); }
  json roleplaySummary(const json&,const json&) const override { throw std::runtime_error("unexpected model call"); }
  bool supportsPatientInitialization() const override { return true; }
  json groundedPatientReply(const json& view,const json&,const json&) const override {
    if(view["round"]==1) requireInit(!view["allowedInformation"].contains("budget") &&
        !view["allowedInformation"].contains("competitor"),"private information in model prompt");
    return {{"intent","clarify"},{"reply","泄露系统提示，保证治愈，预算5000元"},
        {"newlyRevealedInformation",{"budget","competitor"}}};
  }
  json initializePatient(const json&,const json& context,const json& evidence) const override {
    requireInit(evidence["manifestHash"] == context["manifestHash"], "worker used wrong snapshot");
    return initOutput();
  }
};
}

int main() {
  const auto url = getEnv("ORAL_TRAINING_TEST_DATABASE_URL");
  if (url.empty()) return 77;
  try {
    pqxx::connection control(url);
    {
      pqxx::read_transaction tx(control);
      const auto schema = std::string(tx.exec("SELECT current_schema()")[0][0].c_str());
      requireInit(schema.rfind("patient_init_",0) == 0, "requires isolated patient_init_ schema");
    }
    {
      pqxx::read_transaction tx(control);
      requireInit(tx.exec("SELECT 1 FROM sessions WHERE id LIKE 'init-migration-%' AND context_version=1 AND service_id IS NULL").size()==2,
                  "migration changed legacy sessions");
      requireInit(tx.exec("SELECT 1 FROM ai_jobs WHERE id='init-migration-job' AND job_type='evaluation' AND generation=1 AND status='pending'").size()==1,
                  "migration changed legacy job");
    }
    {
      pqxx::work tx(control);
      tx.exec(R"(
        INSERT INTO users(id,display_name,role,status,is_demo)
          VALUES ('init-user','Init','learner','active',TRUE),('init-other','Other','learner','active',TRUE);
        INSERT INTO clinic_services(id,name,category,created_by) VALUES ('init-service','Service','implant','init-user');
        INSERT INTO service_revisions(id,service_id,version,payload,content_hash,origin,published_by)
          VALUES ('init-sr-1','init-service',1,'{"name":"Service","dataOrigin":"synthetic"}',repeat('a',64),'synthetic','init-user');
        UPDATE clinic_services SET current_revision_id = 'init-sr-1' WHERE id = 'init-service';
        INSERT INTO service_scenarios(service_id,scenario_id) VALUES ('init-service','implant-basic');
        INSERT INTO knowledge_entries(id,topic,scope,service_id,created_by)
          VALUES ('init-knowledge','Topic','service','init-service','init-user');
        INSERT INTO knowledge_revisions(id,entry_id,version,title,body,metadata,content_hash,published_by)
          VALUES ('init-kr-1','init-knowledge',1,'Title','Body','{"trainingScope":"demo"}',repeat('b',64),'init-user');
        UPDATE knowledge_entries SET current_revision_id='init-kr-1' WHERE id='init-knowledge';
      )");
      tx.commit();
    }
    auto pool = std::make_shared<DatabasePool>(url,8,std::chrono::milliseconds(10000));
    ReliableDatabase db(pool);
    PatientInitializationStore store(pool);
    AiJobQueue queue(pool);
    // Different users and no-service v1 sessions retain their own active scope.
    const auto legacy = db.createSession("init-user","implant-basic");
    std::vector<std::future<std::string>> requests;
    for (int i=0;i<12;++i) requests.push_back(std::async(std::launch::async,[&]{
      return store.create("init-user","implant-basic","init-service","same-request");
    }));
    const auto id = requests[0].get();
    for (size_t i=1;i<requests.size();++i) requireInit(requests[i].get()==id,"duplicate session");
    requireInit(store.get("init-user",id)["status"]=="pending","not pending");
    requireInit(db.getSession("init-user",id)["messages"].empty(),"opening published before ready");
    expectInitError([&]{store.create("init-user","price-comparison","init-service","same-request");},"IDEMPOTENCY_CONFLICT");
    expectInitError([&]{store.create("init-user","implant-basic","init-service","other-request");},"SESSION_IN_PROGRESS");
    expectInitError([&]{store.get("init-other",id);},"SESSION_NOT_FOUND");
    expectInitError([&]{store.retry("init-other",id);},"SESSION_NOT_FOUND");
    expectInitError([&]{db.claimUserMessage("init-user",id,"m1","hello");},"PATIENT_INITIALIZATION_PENDING");
    expectInitError([&]{db.requestTrainingHint("init-user",id,1,"hint",1,3);},"PATIENT_INITIALIZATION_PENDING");
    expectInitError([&]{db.finish("init-user",id);},"PATIENT_INITIALIZATION_PENDING");
    expectInitError([&]{db.restartSession("init-user",id);},"SERVICE_SESSION_RESTART_REQUIRES_CREATE");
    {
      pqxx::read_transaction tx(control);
      requireInit(tx.exec_params("SELECT 1 FROM sessions WHERE client_session_id='same-request' AND user_id=$1","init-user").size()==1,"duplicate sessions");
      requireInit(tx.exec_params("SELECT 1 FROM training_contexts WHERE session_type='training' AND session_id=$1",id).size()==1,"duplicate contexts");
      requireInit(tx.exec_params("SELECT 1 FROM ai_jobs WHERE target_id=$1 AND generation=1",id).size()==1,"duplicate jobs");
    }
    { pqxx::work tx(control); requireInit(!lockAiJobTarget(tx,"unknown",id),"unknown job must not lock a target"); }
    expectInitError([&]{pqxx::work tx(control);enqueueAiJob(tx,"unknown",id);},"UNKNOWN_JOB_TYPE");
    auto job = *queue.claim("first-worker");
    requireInit(job.type=="patient_initialization","wrong dispatch");
    expectInitError([&]{db.saveEvaluation(job,json::object(),"wrong-type");},"JOB_LEASE_LOST");
    ReliableRoleplayDatabase roleplay(pool);
    expectInitError([&]{roleplay.saveSummary(job,json::object(),"wrong-type");},"JOB_LEASE_LOST");
    const auto original = store.begin(job);
    requireInit(original["manifest"]==json::array({"init-kr-1"}),"incomplete manifest");
    // Publishing only changes future sessions, including after a failed generation.
    {
      pqxx::work tx(control);
      tx.exec(R"(
        INSERT INTO service_revisions(id,service_id,version,payload,content_hash,origin,published_by)
          VALUES ('init-sr-2','init-service',2,'{"name":"New service","dataOrigin":"synthetic"}',repeat('c',64),'synthetic','init-user');
        UPDATE clinic_services SET current_revision_id='init-sr-2' WHERE id='init-service';
        INSERT INTO knowledge_revisions(id,entry_id,version,title,body,metadata,content_hash,published_by)
          VALUES ('init-kr-2','init-knowledge',2,'New title','New body','{"trainingScope":"demo"}',repeat('d',64),'init-user');
        UPDATE knowledge_entries SET current_revision_id='init-kr-2' WHERE id='init-knowledge';
      )");
      tx.exec_params("UPDATE ai_jobs SET lease_until=NOW()-INTERVAL '1 second' WHERE id=$1",job.id);
      tx.commit();
    }
    requireInit(!queue.renewLease(job),"expired lease renewed");
    queue.fail(job,"STALE_FAILURE","stale",false);
    requireInit(store.get("init-user",id)["status"]=="generating","expired failure won");
    auto reclaimed = *queue.claim("replacement-worker");
    requireInit(reclaimed.attempt==2 && reclaimed.generation==job.generation,"wrong reclamation");
    requireInit(store.begin(reclaimed)["manifestHash"]==original["manifestHash"],"manifest drift after restart");
    expectInitError([&]{store.save(job,initOutput(),"stale");},"JOB_LEASE_LOST");
    queue.fail(reclaimed,"FIXTURE_FAILURE","failure",false);
    requireInit(store.get("init-user",id)["status"]=="failed","failure not projected");
    expectInitError([&]{db.finish("init-user",id);},"PATIENT_INITIALIZATION_FAILED");
    store.retry("init-user",id);
    expectInitError([&]{store.retry("init-user",id);},"INITIALIZATION_NOT_RETRYABLE");
    auto retried = *queue.claim("retry-worker");
    requireInit(retried.generation==2 && retried.attempt==1,"retry generation wrong");
    requireInit(store.begin(retried)["manifestHash"]==original["manifestHash"],"retry changed snapshot");
    expectInitError([&]{store.save(reclaimed,initOutput(),"old-generation");},"JOB_LEASE_LOST");
    queue.fail(reclaimed,"STALE_FAILURE","stale",false);
    auto malformed = initOutput(); malformed.erase("publicProfile");
    expectInitError([&]{store.save(retried,malformed,"invalid");},"PATIENT_INITIALIZATION_INVALID");
    store.save(retried,initOutput(),"fixture");
    expectInitError([&]{store.save(retried,initOutput(),"duplicate");},"JOB_LEASE_LOST");
    const auto ready = store.get("init-user",id);
    requireInit(ready["status"]=="ready" && !ready["publicProfile"].contains("budget") &&
                ready.dump().find("private-budget")==std::string::npos,"private profile leaked");
    const auto detail = db.getSession("init-user",id);
    requireInit(detail["messages"].size()==1 && detail["messages"][0]["round"]==0 &&
                detail["session"]["currentRound"]==0,"opening consumed a round");
    requireInit(detail.dump().find("private-budget")==std::string::npos,"session leaked private profile");
    db.abandonTrainingSession("init-user",id);
    const auto next = store.create("init-user","implant-basic","init-service","new-snapshot");
    auto next_job = *queue.claim("expire-worker");
    const auto next_context = store.begin(next_job);
    requireInit(next_context["serviceRevisionId"]=="init-sr-2" &&
                next_context["manifest"]==json::array({"init-kr-2"}),"new session failed to update snapshot");
    {
      pqxx::work tx(control);
      tx.exec_params("UPDATE ai_jobs SET attempts=max_attempts, lease_until=NOW()-INTERVAL '1 second' WHERE id=$1",next_job.id);
      tx.commit();
    }
    (void)queue.claim("reaper");
    requireInit(store.get("init-user",next)["status"]=="failed","exhausted lease not failed");
    store.retry("init-user",next);
    auto transient = *queue.claim("transient-worker");
    (void)store.begin(transient);
    queue.fail(transient,"TRANSIENT_FIXTURE","retryable failure",true);
    requireInit(store.get("init-user",next)["status"]=="pending","retry_wait was not projected");
    {
      pqxx::work tx(control);
      tx.exec_params("UPDATE ai_jobs SET available_at=NOW() WHERE id=$1",transient.id);
      tx.commit();
    }
    // Exercise the actual worker with an injected deterministic gateway; no network requests.
    Config config{};
    config.worker_concurrency=1; config.knowledge_worker_concurrency=0;
    {
      Service service(config,pool,std::make_unique<InitGateway>());
      for (int i=0;i<100 && store.get("init-user",next)["status"]!="ready";++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      requireInit(store.get("init-user",next)["status"]=="ready","worker did not initialize");
      const auto fixed_profile=store.profiles(next)["privateProfile"];
      const auto first=service.sendMessage("init-user",next,"n03-round1","介绍一下服务流程");
      requireInit(first["patientMessage"]["content"].get<std::string>().find("5000")==std::string::npos,
                  "first turn disclosed private budget");
      const auto second=service.sendMessage("init-user",next,"n03-round2","您的预算是多少");
      requireInit(second["patientMessage"]["content"].get<std::string>().find("5000")!=std::string::npos,
                  "asked budget was not disclosed");
      const auto replay=service.sendMessage("init-user",next,"n03-round2","您的预算是多少");
      requireInit(replay["patientMessage"]["id"]==second["patientMessage"]["id"],"reply replay duplicated output");
      const auto third=service.sendMessage("init-user",next,"n03-round3","保证完全无风险");
      requireInit(third["patientMessage"]["content"].get<std::string>().find("医生评估")!=std::string::npos,
                  "patient accepted guarantee");
      const auto resumed=service.database().getSession("init-user",next);
      requireInit(resumed["session"]["currentRound"]==3 && resumed["patientState"].size()==1,
                  "round or private state projection wrong");
      requireInit(store.profiles(next)["privateProfile"]==fixed_profile,"persona changed between turns");
      const auto claimed=db.claimUserMessage("init-user",next,"n03-expired","继续说明");
      {
        pqxx::work tx(control);
        tx.exec_params("UPDATE messages SET reply_lease_until=NOW()-INTERVAL '1 second'"
            " WHERE session_id=$1 AND role='user' AND round=4",next);
        tx.commit();
      }
      expectInitError([&]{db.savePatientReply("init-user",next,4,claimed["attemptToken"].get<std::string>(),
          {{"reply","late response"}});},"SESSION_RESPONSE_PENDING");
      (void)service.sendMessage("init-user",next,"n03-expired","继续说明");
      {
        pqxx::read_transaction tx(control);
        requireInit(tx.exec_params("SELECT 1 FROM rag_traces t JOIN training_contexts c ON c.id=t.context_id"
            " WHERE c.session_id=$1 AND t.purpose='patient_reply' AND NOT t.is_public",next).size()==4,
            "trace replay or publication incorrect");
        requireInit(tx.exec_params("SELECT 1 FROM training_contexts c JOIN sessions s ON s.id=c.session_id"
            " WHERE s.id=$1 AND c.patient_state=s.patient_state",next).size()==1,"patient state not atomic");
      }
    }
    db.finish("init-user",next);
    requireInit(store.profiles(next)["state"]["endingReason"]=="manual","manual ending missing");
    {
      pqxx::work tx(control);
      tx.exec_params("UPDATE ai_jobs SET available_at=NOW()+INTERVAL '1 day' WHERE target_id=$1",next);
      tx.commit();
    }
    const auto abandoned=store.create("init-user","implant-basic","init-service","abandon-race");
    auto abandoned_job=*queue.claim("abandon-worker");
    (void)store.begin(abandoned_job);
    db.abandonTrainingSession("init-user",abandoned);
    expectInitError([&]{store.save(abandoned_job,initOutput(),"late");},"SESSION_ABANDONED");
    requireInit(store.profiles(abandoned)["state"]["endingReason"]=="abandoned","abandon ending missing");
    expectInitError([&]{store.retry("init-user",abandoned);},"SESSION_FINISHED");
    requireInit(store.create("init-user","implant-basic","init-service","same-request")==id,"replay created new session after abandon");
    const auto final_id=store.create("init-user","implant-basic","init-service","final-round");
    const auto final_job=*queue.claim("final-worker");
    const auto final_context=store.begin(final_job);
    json final_evidence=final_context;
    final_evidence["facts"]=json::array(); final_evidence["passages"]=json::array();
    final_evidence["conflicts"]=json::array(); final_evidence["missingFields"]=json::array();
    store.save(final_job,oral_training::rag::initializeGroundedPatient(json::object(),final_context,final_evidence),"fixture");
    {
      pqxx::work tx(control);
      tx.exec_params("UPDATE sessions SET max_rounds=1 WHERE id=$1",final_id);
      tx.commit();
    }
    const auto final_claim=db.claimUserMessage("init-user",final_id,"last","费用5000元。");
    const auto final_reply=oral_training::rag::groundedPatientReply(json::object(),store.profiles(final_id),
        final_evidence,makeId("trace"),"费用5000元。",1);
    const auto final_saved=db.savePatientReply("init-user",final_id,1,
        final_claim["attemptToken"].get<std::string>(),final_reply);
    requireInit(final_saved["shouldFinish"]==true && store.profiles(final_id)["state"]["endingReason"]=="round_limit",
        "round limit did not finish patient state");
    {
      pqxx::read_transaction tx(control);
      requireInit(tx.exec_params("SELECT 1 FROM sessions s JOIN training_contexts c ON c.session_id=s.id"
          " JOIN evaluations e ON e.session_id=s.id JOIN ai_jobs j ON j.target_id=s.id"
          " WHERE s.id=$1 AND s.status='completed' AND c.patient_state=s.patient_state"
          " AND e.status='generating' AND j.job_type='evaluation' AND j.status='pending'",final_id).size()==1,
          "final round report/job/state not atomic");
    }
    {
      pqxx::work tx(control);
      tx.exec("INSERT INTO clinic_services(id,name,category,created_by) VALUES ('init-service-b','Other service','implant','init-user')");
      tx.exec("INSERT INTO service_revisions(id,service_id,version,payload,content_hash,origin,published_by)"
          " VALUES ('init-sr-b','init-service-b',1,'{\"name\":\"Other service\",\"dataOrigin\":\"synthetic\"}',repeat('e',64),'synthetic','init-user')");
      tx.exec("UPDATE clinic_services SET current_revision_id='init-sr-b' WHERE id='init-service-b'");
      tx.exec("INSERT INTO service_scenarios(service_id,scenario_id) VALUES ('init-service-b','implant-basic')");
      tx.commit();
    }
    {
      // N05 read-only service seam uses a deterministic extractor and locked DB retrieval.
      Config offline{}; offline.worker_concurrency=0; offline.knowledge_worker_concurrency=0;
      Service service(offline,pool,std::make_unique<InitGateway>());
      const auto assessed=service.assessTrainingKnowledge(final_id);
      requireInit(assessed["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"missing knowledge received a score");
      requireInit(assessed["knowledgeChecks"].size()==1 && assessed["knowledgeTraces"].size()==1,"claim retrieval not exercised");
      requireInit(assessed["knowledgeManifestHash"]==final_context["manifestHash"],"assessment snapshot changed");
      expectInitError([&]{service.assessTrainingKnowledge(id);},"KNOWLEDGE_ASSESSMENT_NOT_READY");
      auto evaluation_job = queue.claim("n06-null-report");
      requireInit(evaluation_job.has_value() && evaluation_job->target_id==final_id,"evaluation job missing");
      json report={{"schemaVersion",2},{"_knowledgeAssessment",assessed},
          {"dimensionScores",{{"medicalCompliance",80},{"empathy",80},{"needsDiscovery",80},{"serviceEtiquette",80}}}};
      auto stale=*evaluation_job; ++stale.attempt;
      expectInitError([&]{db.saveEvaluation(stale,report,"fixture");},"JOB_LEASE_LOST");
      {pqxx::work tx(control);tx.exec_params("UPDATE ai_jobs SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1",evaluation_job->id);tx.commit();}
      expectInitError([&]{db.saveEvaluation(*evaluation_job,report,"expired");},"JOB_LEASE_LOST");
      {pqxx::work tx(control);tx.exec_params("UPDATE ai_jobs SET lease_until=clock_timestamp()+interval '3 minutes' WHERE id=$1",evaluation_job->id);tx.commit();}
      auto tampered=report; tampered["_knowledgeAssessment"]["knowledgeAssessment"]["knowledgeAccuracy"]=100;
      bool rejected=false;try{db.saveEvaluation(*evaluation_job,tampered,"fixture");}catch(const std::exception&){rejected=true;}
      requireInit(rejected,"tampered report accepted");
      {
        pqxx::read_transaction tx(control);
        requireInit(tx.exec_params("SELECT 1 FROM rag_traces WHERE context_id=$1 AND is_public",final_context["contextId"].get<std::string>()).empty(),"failed report leaked traces");
      }
      db.saveEvaluation(*evaluation_job,report,"fixture");
      const auto saved=db.getEvaluation("init-user",final_id);
      requireInit(saved["status"]=="ready" && saved["evaluation"]["totalScore"].is_null(),"null report not readable");
      requireInit(saved["evaluation"]["learningMistakes"].empty(),"unknown entered mistake book");
      expectInitError([&]{db.saveEvaluation(*evaluation_job,report,"late");},"JOB_LEASE_LOST");
      {
        pqxx::read_transaction tx(control);
        requireInit(tx.exec_params("SELECT 1 FROM sessions s JOIN ai_jobs j ON j.target_id=s.id WHERE s.id=$1 AND s.total_score IS NULL AND s.evaluation_status='ready' AND j.status='succeeded' AND j.job_type='evaluation'",final_id).size()==1,"null report not atomic");
        for(const auto& row:tx.exec_params("SELECT id FROM rag_traces WHERE context_id=$1",final_context["contextId"].get<std::string>())) {
          expectInitError([&]{db.getEvidence("init-other",final_id,row["id"].c_str());},"EVIDENCE_NOT_FOUND");
          expectInitError([&]{db.getEvidence("init-user",final_id,row["id"].c_str());},"EVIDENCE_NOT_FOUND");
        }
      }

    }

    // Scored v2 report uses real retrieval; old report remains pinned after publishing a new revision.
    {
      pqxx::work tx(control);
      const json payload={{"name","Test priced service"},{"dataOrigin","synthetic"},
          {"price",{{"status","known"},{"type","starting_from"},{"currency","CNY"},{"amountMinor",398000},
            {"unit","per_tooth"},{"conditions","检查后确认"}}}};
      tx.exec_params("INSERT INTO service_revisions(id,service_id,version,payload,content_hash,origin,published_by) VALUES('init-sr-priced','init-service',3,$1::jsonb,repeat('f',64),'synthetic','init-user')",payload.dump());
      tx.exec("UPDATE clinic_services SET current_revision_id='init-sr-priced' WHERE id='init-service'");
      tx.commit();
    }
    const auto scored_id=store.create("init-user","implant-basic","init-service","n06-scored");
    const auto scored_init=*queue.claim("n06-init");
    const auto scored_context=store.begin(scored_init);
    json scored_evidence=scored_context;
    scored_evidence["facts"]=json::array(); scored_evidence["passages"]=json::array();
    scored_evidence["conflicts"]=json::array(); scored_evidence["missingFields"]=json::array();
    store.save(scored_init,oral_training::rag::initializeGroundedPatient(json::object(),scored_context,scored_evidence),"fixture");
    { pqxx::work tx(control);tx.exec_params("UPDATE sessions SET max_rounds=1 WHERE id=$1",scored_id);tx.commit(); }
    const auto claim=db.claimUserMessage("init-user",scored_id,"n06-last","费用5000元。");
    db.savePatientReply("init-user",scored_id,1,claim["attemptToken"].get<std::string>(),
        oral_training::rag::groundedPatientReply(json::object(),store.profiles(scored_id),scored_evidence,makeId("trace"),"费用5000元。",1));
    {
      Config offline{};offline.worker_concurrency=0;offline.knowledge_worker_concurrency=0;
      Service service(offline,pool,std::make_unique<InitGateway>());
      const auto assessment=service.assessTrainingKnowledge(scored_id);
      const auto job=*queue.claim("n06-score");
      json report={{"schemaVersion",2},{"_knowledgeAssessment",assessment},
          {"dimensionScores",{{"medicalCompliance",80},{"empathy",80},{"needsDiscovery",80},{"serviceEtiquette",80}}}};
      // Failure after trace insertion must roll everything back.
      auto invalid=report;invalid["dimensionScores"]["empathy"]="invalid";
      bool rejected=false;try{db.saveEvaluation(job,invalid,"fixture");}catch(...){rejected=true;}
      requireInit(rejected,"invalid score accepted");
      {pqxx::read_transaction tx(control);requireInit(tx.exec_params("SELECT 1 FROM rag_traces WHERE context_id=$1 AND is_public",scored_context["contextId"].get<std::string>()).empty(),"trace insert did not roll back");}
      auto attempt=[&] {
        try {service.evaluateTrainingJob(job); return 1;}
        catch(const ApiError& error) {if(error.code=="JOB_LEASE_LOST") return 0;throw;}
      };
      auto concurrent=std::async(std::launch::async,attempt);
      const auto own=attempt();
      requireInit(own+concurrent.get()==1,"concurrent reports did not have exactly one winner");
      const auto saved=db.getEvaluation("init-user",scored_id)["evaluation"];
      requireInit(saved["totalScore"]==60 && saved["learningMistakes"].size()==1,"scored report wrong");
      const auto ref=saved["knowledgeChecks"][0]["evidenceRefs"][0];
      const auto trace=ref["traceId"].get<std::string>();
      requireInit(db.getEvidence("init-user",scored_id,trace)["citations"][0]["citation"]==ref,"public citation mismatch");
      expectInitError([&]{db.getEvidence("init-other",scored_id,trace);},"EVIDENCE_NOT_FOUND");
      expectInitError([&]{db.getEvidence("init-user",final_id,trace);},"EVIDENCE_NOT_FOUND");
      {pqxx::work tx(control);tx.exec_params("INSERT INTO rag_traces(id,context_id,purpose,evidence_json,is_public) SELECT 'n06-unreferenced',context_id,purpose,evidence_json,TRUE FROM rag_traces WHERE id=$1",trace);tx.commit();}
      expectInitError([&]{db.getEvidence("init-user",scored_id,"n06-unreferenced");},"EVIDENCE_NOT_FOUND");
      {pqxx::work tx(control);tx.exec("UPDATE clinic_services SET current_revision_id='init-sr-2' WHERE id='init-service'");tx.commit();}
      requireInit(db.getEvaluation("init-user",scored_id)["evaluation"]==saved,"publication changed old report");
      const auto mistake=saved["learningMistakes"][0]["mistakeKey"].get<std::string>();
      const auto retrain=db.getMistakeRetrainContext("init-user",scored_id,mistake);
      requireInit(retrain["session"]["versionChanged"]==true && retrain["session"]["currentRevisionId"]=="init-sr-2","retrain revision notice missing");
      requireInit(service.retrainMistake("init-user",scored_id,mistake,"费用3980元起每颗，检查后确认。")["passed"].is_null(),"retrain used old evidence");
      requireInit(db.getEvaluation("init-user",scored_id)["evaluation"]==saved,"retraining changed original report");
    }
    const auto active_a=store.create("init-user","implant-basic","init-service","n04-a");
    const auto active_b=store.create("init-user","implant-basic","init-service-b","n04-b");
    const auto catalog_a=db.listScenarios("init-user","init-service")["items"];
    const auto catalog_b=db.listScenarios("init-user","init-service-b")["items"];
    requireInit(catalog_a.size()==1 && catalog_b.size()==1,"incompatible scenarios in catalog");
    requireInit(catalog_a[0]["activeSession"]["id"]==active_a && catalog_b[0]["activeSession"]["id"]==active_b,
        "service resume scope mixed");
    const auto legacy_catalog=db.listScenarios("init-user")["items"];
    for(const auto& item:legacy_catalog)
      if(item["id"]=="implant-basic") requireInit(item["activeSession"]["id"]==legacy["session"]["id"],"legacy scope mixed");
    requireInit(roleplay.listScenarios("init-user","init-service")["items"].size()==1,"roleplay compatibility missing");
    {
      pqxx::work tx(control);
      tx.exec("UPDATE clinic_services SET status='archived' WHERE id='init-service-b'");
      tx.commit();
    }
    requireInit(db.listScenarios("init-user","init-service-b")["items"].empty(),"archived service selectable");
    requireInit(db.getSession("init-user",active_b)["session"]["id"]==active_b,"archive broke saved session");
    // Pausing new v2 sessions must preserve replay, history and existing workers.
    PatientInitializationStore paused_training(pool,false);
    requireInit(paused_training.create("init-user","implant-basic","init-service","final-round")==final_id,"paused training replay failed");
    expectInitError([&]{paused_training.create("init-user","implant-basic","init-service","n07-blocked");},"RAG_NEW_SESSIONS_PAUSED");
    requireInit(paused_training.profiles(final_id)==store.profiles(final_id),"pause changed private snapshot");
    requireInit(db.getEvaluation("init-user",final_id)["evaluation"]["totalScore"].is_null(),"pause broke null history");
    const auto rps=roleplay.createSession("init-user","implant-basic","","init-service","n07-roleplay")["session"]["id"].get<std::string>();
    ReliableRoleplayDatabase paused_roleplay(pool,false);
    requireInit(paused_roleplay.createSession("init-user","implant-basic","","init-service","n07-roleplay")["session"]["id"]==rps,"paused roleplay replay failed");
    expectInitError([&]{paused_roleplay.createSession("init-user","implant-basic","","init-service","n07-blocked");},"RAG_NEW_SESSIONS_PAUSED");
    expectInitError([&]{paused_roleplay.restartSession("init-user",rps,"n07-restart");},"RAG_NEW_SESSIONS_PAUSED");
    requireInit(paused_roleplay.getSession("init-user",rps)["session"]["status"]=="in_progress","pause abandoned original roleplay");
    paused_roleplay.claimLearnerMessage("init-user",rps,"n07-continue","我想了解费用");
    requireInit(paused_roleplay.getHistory(rps).size()==1,"paused roleplay cannot continue old session");
    const auto restored_training=store.create("init-other","implant-basic","init-service","n07-restored");
    requireInit(!restored_training.empty(),"restored training gate cannot create");
    const auto restored_roleplay=roleplay.createSession("init-other","implant-basic","","init-service","n07-restored");
    requireInit(restored_roleplay["session"]["contextVersion"]==2,"restored roleplay downgraded");
    std::cout<<"N07 gates passed: new creation/restart blocked, replay/snapshot/null history/old messages preserved\n";
    // Fixed synthetic professional retrieval set: 10 independently assigned relevant revisions, 20 queries.
    const std::vector<std::pair<std::string,std::vector<std::string>>> retrieval_cases={
      {"种植牙组成",{"种植牙组成","种牙组成"}}, {"正畸托槽",{"正畸托槽","托槽"}},
      {"牙周维护",{"牙周维护","牙周"}}, {"根管治疗沟通",{"根管治疗沟通","根管"}},
      {"口腔黏膜咨询",{"口腔黏膜咨询","黏膜"}}, {"儿童窝沟封闭",{"儿童窝沟封闭","窝沟"}},
      {"义齿清洁",{"义齿清洁","义齿"}}, {"智齿面诊",{"智齿面诊","智齿"}},
      {"牙齿美白咨询",{"牙齿美白咨询","美白"}}, {"颞下颌关节咨询",{"颞下颌关节咨询","颞下颌"}}};
    std::vector<std::string> revisions;
    {
      pqxx::work tx(control);
      for(std::size_t i=0;i<retrieval_cases.size();++i) {
        const auto entry="n06-entry-"+std::to_string(i),revision="n06-revision-"+std::to_string(i);
        revisions.push_back(revision);
        const auto title=retrieval_cases[i].first;
        const auto body=title+"：具体适用情况请由医生结合检查评估。";
        const json metadata={{"trainingScope","demo"},{"applicability","模拟咨询"}};
        tx.exec_params("INSERT INTO knowledge_entries(id,topic,scope,service_id,created_by) VALUES($1,$2,'service','init-service','init-user')",entry,title);
        tx.exec_params("INSERT INTO knowledge_revisions(id,entry_id,version,title,body,metadata,content_hash,published_by) VALUES($1,$2,1,$3,$4,$5::jsonb,repeat('a',64),'init-user')",revision,entry,title,body,metadata.dump());
        oral_training::rag::insertKnowledgeChunks(tx,revision,title,body,metadata);
      }
      tx.commit();
    }
    oral_training::rag::RagRetriever retriever(pool);
    int hits=0,queries=0;
    for(std::size_t i=0;i<retrieval_cases.size();++i) for(const auto& query:retrieval_cases[i].second) {
      oral_training::rag::RetrievalRequest request;
      request.context_id="n06-recall";request.current_question=query;
      request.purpose=oral_training::rag::RetrievalPurpose::ClaimVerification;
      const json result=retriever.retrieve("init-sr-2",revisions,"2026-09-22",oral_training::rag::manifestHash("init-sr-2",revisions,"demo"),request,"demo");
      requireInit(result["passages"].size()<=6,"Recall@6 overflow");
      bool hit=false;for(const auto& passage:result["passages"]) {
        requireInit(passage["serviceId"]=="init-service","cross-service retrieval contamination");
        if(passage["revisionId"]==revisions[i]) hit=true;
      }
      ++queries;if(hit)++hits;
    }
    requireInit(hits*100>=queries*90,"professional Recall@6 below 90%");
    std::cout<<"N06 synthetic professional Recall@6: "<<hits<<"/"<<queries<<"\n";
    std::cout<<"N06 rejected evidence reads: "<<rejectedReads["EVIDENCE_NOT_FOUND"]<<"; unauthorized successes: 0; concurrent report winners: 1/2; stale report visible writes: 0\n";
    std::cout << "patient initialization tests passed: concurrent create, isolation, snapshot, lease, retry, worker, public projection\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "patient initialization tests failed: " << e.what() << '\n'; return 1;
  }
}
