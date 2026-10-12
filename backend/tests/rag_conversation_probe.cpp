#include "embedding_fixture.h"
#include "live_model_fixture.h"
#include "../src/service_reply_prompt.h"
#include "../src/patient_prompt.h"
#include "../src/patient_grounding.h"
#include <fstream>
#include <map>

int main(int argc, char** argv) {
  if (argc != 5 || std::string(argv[1]) != "--live") {
    std::cerr << "--live <knowledge.json> <conversations.json> <development|holdout|all>\n"; return 2;
  }
  try {
    const auto* url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
    requireEmbedding(url, "Missing isolated database");
    LiveModelFixture model;
    json corpus, suite;
    std::ifstream(argv[2]) >> corpus; std::ifstream(argv[3]) >> suite;
    const std::string cohort = argv[4];
    requireEmbedding(cohort=="all" || cohort=="development" || cohort=="holdout", "Unknown cohort");
    std::map<std::string,std::unique_ptr<EmbeddingFixture>> fixtures;
    std::map<std::string,std::map<std::string,std::string>> fixture_topics;
    for (const auto& conversation : suite.at("conversations")) {
      if (cohort != "all" && conversation.at("cohort") != cohort) continue;
      const bool patient = conversation.value("role","customer") == "patient";
      const auto payload=conversation.value("servicePayload",json::object());
      const auto type = conversation.value("corpus","default");
      const auto fixture_key=type+":"+payload.dump();
      const bool fresh=fixtures.count(fixture_key)==0;
      if(fresh) fixtures[fixture_key]=std::make_unique<EmbeddingFixture>(url,payload);
      auto& fixture=*fixtures.at(fixture_key);
      auto& topics=fixture_topics[fixture_key];
      // Reuse only immutable knowledge vectors. Dialogue, persona and context IDs
      // remain separate; no generation result or private state is shared.
      if (fresh && type == "default") {
        for (const auto& item : corpus.at("knowledgeItems")) {
          const auto revision = fixture.add(item.at("title"), item.at("body"), item.at("metadata"), item.at("topic"));
          topics[revision] = item.at("topic");
        }
      } else if (fresh && type == "conflicting") {
        for (const auto& text : {"本服务复查包含拍片。", "本服务复查不包含拍片。"})
          topics[fixture.add("复查政策",text,{},"review-policy")] = "review-policy";
      } else requireEmbedding(!fresh || type=="empty", "Unknown corpus");
      json context=fixture.context();
      context["contextId"]=fixture.prefix+"-"+conversation.at("id").get<std::string>();
      RagRetriever retriever(fixture.pool);
      const json scenario = {{"public",{{"title","合成口腔客服双角色多轮测试"},{"description","全部知识为虚构测试资料"}}}};
      json history=json::array(), previous_replies=json::array(), profiles;
      std::string previous;
      const auto base_record = [&](const std::string& id, const std::string& question) {
        return json{{"conversationId",conversation.at("id")},{"turnId",id},{"role",patient?"patient":"customer"},
          {"cohort",conversation.at("cohort")},{"question",question},{"suiteVersion",suite.at("version")},
          {"context",context},{"promptVersion",patient?prompt_version::kPatientReply:prompt_version::kServiceReply}};
      };
      const auto retrieve = [&](RetrievalRequest query,json& record) {
        query.context_id=context["contextId"].get<std::string>();
        const json evidence=retriever.retrieve(fixture.revision,fixture.manifest,"2026-10-08T12:00:00+08:00",fixture.hash(),query);
        record["retrievalQuery"]=customerRetrievalQuestion(query);
        record["evidence"]=evidence;
        record["retrievedTopics"]=json::array(); record["retrievedFields"]=json::array();
        for (const auto& p:evidence["passages"]) record["retrievedTopics"].push_back(topics.at(p["revisionId"]));
        for (const auto& f:evidence["facts"]) record["retrievedFields"].push_back(f["field"]);
        return evidence;
      };
      if (patient) {
        auto record=base_record("__initialization__", "选择虚构患者画像");
        record["promptVersion"]=prompt_version::kPatientInitialization;
        const auto start=std::chrono::steady_clock::now();
        RetrievalRequest request; request.purpose=RetrievalPurpose::PatientInitialization; request.current_question="所选种植牙服务的价格、时间和流程";
        const auto evidence=retrieve(request,record);
        try {
          requireEmbedding(fixture.pool->stats().in_use==0,"Model call holds database lease");
          const auto output=model.complete(oral_training::models::patientInitializationMessages(scenario,context,evidence),6000,0.45,false,record);
          const auto initialized=initializeGroundedPatient(output,context,evidence);
          record["result"]=initialized;
          profiles={{"context",context},{"publicProfile",initialized["publicProfile"]},
              {"privateProfile",initialized["privateProfile"]},{"state",initialized["patientState"]}};
          history.push_back({{"role","patient"},{"round",0},{"content",initialized["opening"]}});
        } catch (const std::exception& error) {
          record["error"]=error.what(); std::cout<<record.dump()<<std::endl; throw;
        }
        record["elapsedMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        record["httpCalls"]=model.calls; std::cout<<record.dump()<<std::endl;
      }
      int round=0; bool ended=false;
      for (const auto& turn : conversation.at("turns")) {
        ++round;
        const auto started = std::chrono::steady_clock::now();
        const std::string question = turn.at("question");
        history.push_back({{"role",patient?"user":"learner_patient"},{"round",round},{"content",question}});
        auto record=base_record(turn.at("id"),question); record["round"]=round;
        try {
          RetrievalRequest query; query.current_question=question; query.recent_question_answers=history;
          query.purpose=patient?RetrievalPurpose::PatientReply:RetrievalPurpose::CustomerReply;
          const auto evidence=retrieve(query,record);
          record["retrievalMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
          json messages;
          if(patient) {
            record["beforeState"]=profiles["state"];
            record["persona"]=profiles["privateProfile"]; // Synthetic audit only; never included in model request.
            record["modelView"]=patientView(profiles,question,round);
            messages=oral_training::models::patientReplyMessages(record["modelView"],history,evidence);
          } else messages=oral_training::models::serviceReplyMessages(scenario,history,evidence);
          requireEmbedding(fixture.pool->stats().in_use==0,"Model call holds database lease");
          const auto output=model.complete(messages,patient?6000:900,patient?0.45:0.15,!patient,record);
          const auto trace="multi-"+std::to_string(model.calls);
          const auto result=patient?groundedPatientReply(output,profiles,evidence,trace,question,round)
              :groundedReply(output,context,evidence,trace,question,previous,previous_replies);
          record["result"]=result;
          previous=result.at("reply"); previous_replies.push_back(previous);
          if(patient) { profiles["state"]=result["patientState"]; ended=result.value("shouldEnd",false); }
          history.push_back({{"role",patient?"patient":"standard_customer"},{"round",round},{"content",previous}});
        } catch (const std::exception& error) {
          record["error"]=error.what();
          if (!record.contains("httpStatus") || record["httpStatus"]!=200) {
            std::cout<<record.dump()<<std::endl; throw;
          }
          previous="当前回答生成失败，请重新提问。";
          history.push_back({{"role",patient?"patient":"standard_customer"},{"round",round},{"content",previous}});
        }
        record["httpCalls"]=model.calls;
        record["elapsedMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::cout<<record.dump()<<std::endl;
        if(ended) break; // Match the production session boundary. Missing later turns remain test failures.
      }
    }
    return 0;
  } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
