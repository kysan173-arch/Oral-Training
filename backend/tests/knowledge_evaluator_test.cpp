#include "../src/knowledge_report.h"
#include "fixtures/knowledge_cases.h"
#include <iostream>
using namespace oral_training::rag;
int main() {
  int checks=0;
  const auto check=[&](bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);};
  try {
    json context={{"contextId","ctx"},{"serviceId","svc"},{"serviceRevisionId","sr"},
        {"manifest",json::array({"kr"})},{"trainingScope","demo"}};
    context["manifestHash"]=manifestHash("sr",context["manifest"],"demo");
    json price={{"status","known"},{"type","starting_from"},{"currency","CNY"},{"amountMinor",398000},
        {"unit","per_tooth"},{"conditions","检查后确认"}};
    json duration={{"status","known"},{"unit","month"},{"minimum",3},{"maximum",6},
        {"estimated",true},{"conditions","因骨量而异"}};
    json appointment={{"status","known"},{"text","工作日预约咨询"},{"timezone","Asia/Shanghai"},{"isLiveAvailability",false}};
    json bundle=context;
    bundle["facts"]=json::array({{{"evidenceId","E1"},{"revisionId","sr"},{"field","price"},{"value",price}},
      {{"evidenceId","E2"},{"revisionId","sr"},{"field","treatmentDuration"},{"value",duration}},
      {{"evidenceId","E3"},{"revisionId","sr"},{"field","appointment"},{"value",appointment}},
      {{"evidenceId","E4"},{"revisionId","sr"},{"field","includedItems"},{"value",json::array({"检查","复诊"})}}});
    bundle["passages"]=json::array({{{"evidenceId","E5"},{"revisionId","kr"},{"chunkId","kr-0"},
        {"scope","service"},{"serviceId","svc"},{"trainingScope","demo"},{"body","具体适用情况需要医生评估。"}}});
    bundle["conflicts"]=json::array(); bundle["missingFields"]=json::array();
    const auto provider=[&](const std::string&,const std::string&){return bundle;};
    const auto run=[&](const std::string& text,json candidates=json::array()) {
      return evaluateKnowledge(context,json::array({{{"role","user"},{"round",1},{"content",text}}}),candidates,provider);
    };
    const auto verdict=[&](const std::string& text) {return run(text)["knowledgeChecks"][0]["verdict"];};
    check(verdict("3980元起每颗，检查后确认。")=="supported","supported price");
    check(verdict("就是3980元。")=="incomplete","missing price qualifiers");
    check(verdict("5000元每颗。")=="contradicted","wrong price");
    check(verdict("3980元起每次，检查后确认。")=="contradicted","wrong unit");
    check(verdict("3980元起每颗，无条件。")=="contradicted","removed condition");
    check(verdict("费用最多3980元每颗。")=="contradicted","starting price turned into cap");
    check(verdict("不是5000元，而是3980元起每颗，检查后确认。")=="supported","negated correction");
    check(verdict("您说不是5000元，而是3980元起每颗，检查后确认。")=="not_applicable","reported correction");
    for(const auto* text:{"您说别家5000元。","如果5000元呢？","不是5000元。","5000元吗？","我的预算5000元。","听说5000元。","您的预算5000元。","不收5000元。"})
      check(verdict(text)=="not_applicable","quotation/modality must not score");
    check(verdict("疗程三个月一定完成。")=="contradicted","guaranteed estimated duration");
    check(verdict("疗程预计3—6个月，因骨量而异。")=="supported","time range");
    check(verdict("疗程3—6个月。")=="incomplete","estimated condition missing");
    check(verdict("疗程预计3—6天，因骨量而异。")=="contradicted","time unit");
    check(verdict("疗程需要医生评估。")=="not_applicable","doctor deferral");
    check(verdict("我现在给您锁定明天下午。")=="contradicted","snapshot is not live slots");
    check(verdict("预约请以预约确认为准。")=="supported","appointment qualification");
    check(verdict("包含检查、复诊。")=="supported","included items");
    check(verdict("不包含检查、复诊。")=="contradicted","excluded included items");
    check(verdict("包含检查。")=="incomplete","missing inclusion");
    check(verdict("包含检查、复诊、住院。")=="evidence_missing","unproven extra inclusion");
    const auto malicious=json::array({{{"round",1},{"originalQuote","虚构报价1元"},{"field","price"},{"scoreImpact",100}},
        {{"round",1},{"originalQuote","5000元"},{"field","price"},{"verdict","supported"},{"scoreImpact",0}}});
    const auto bad=run("5000元。",malicious);
    check(bad["knowledgeChecks"].size()==1,"invented claim included");
    check(bad["knowledgeAssessment"]["knowledgeAccuracy"]==0,"model decided score");
    check(bad["knowledgeChecks"][0]["originalQuote"]=="5000元。","source context truncated");
    check(bad["knowledgeChecks"][0]["evidenceRefs"][0]["manifestHash"]==context["manifestHash"],"citation hash lost");
    check(bad["knowledgeChecks"][0]["evidenceRefs"][0]["evidenceId"]=="E1","wrong field evidence");
    auto no_evidence=bundle; no_evidence["facts"]=json::array(); no_evidence["passages"]=json::array();
    const json history=json::array({{{"role","user"},{"round",1},{"content","5000元。"}}});
    auto none=evaluateKnowledge(context,history,json::array(),[&](const std::string&,const std::string&){return no_evidence;});
    check(none["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"no evidence scored");
    auto conflict=bundle; conflict["conflicts"]=json::array({"inconsistent source"});
    auto conflicted=evaluateKnowledge(context,history,json::array(),[&](const std::string&,const std::string&){return conflict;});
    check(conflicted["knowledgeChecks"][0]["verdict"]=="conflicted" &&
        conflicted["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"conflict in denominator");
    json repeats=history;
    repeats.push_back({{"role","user"},{"round",2},{"content","5000元。"}});
    auto repeated=evaluateKnowledge(context,repeats,json::array(),provider);
    check(repeated["knowledgeAssessment"]["assessableCount"]==1,"repeat double count");
    repeats.push_back({{"role","user"},{"round",3},{"content","更正，3980元起每颗，检查后确认。"}});
    auto corrected=evaluateKnowledge(context,repeats,json::array(),provider);
    check(corrected["knowledgeAssessment"]["knowledgeAccuracy"]==100,"correction ignored");
    check(corrected["knowledgeChecks"][1]["correctedInLaterRound"]==true,"correction process missing");
    check(corrected["knowledgeChecks"][0]["correctedInLaterRound"]==true,"older duplicate correction missing");
    check(corrected["knowledgeChecks"].size()==3,"process discarded");
    check(evaluateKnowledge(context,repeats,malicious,provider)==corrected,"same content nondeterministic");
    json patient_only=json::array({{{"role","patient"},{"round",1},{"content","5000元。"}}});
    check(evaluateKnowledge(context,patient_only,malicious,provider)["knowledgeChecks"].empty(),"patient quote used as learner claim");
    json question=json::array({{{"role","patient"},{"round",0},{"content","价格多少？"}},
        {{"role","user"},{"round",1},{"content","您好。"}}});
    check(evaluateKnowledge(context,question,json::array(),provider)["knowledgeAssessment"]["knowledgeAccuracy"]==0,"unanswered knowledge not counted");
    question[0]["content"]="疗程多久？";question[1]["content"]="疗程需要医生评估。";
    check(evaluateKnowledge(context,question,json::array(),provider)["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"doctor deferral penalized");
    question[1]["content"]="具体情况需要医生评估。";
    check(evaluateKnowledge(context,question,json::array(),provider)["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"contextual doctor deferral penalized");
    question[0]["round"]=2;
    check(evaluateKnowledge(context,question,json::array(),provider)["knowledgeAssessment"]["knowledgeAccuracy"].is_null(),"last patient turn requires future answer");
    auto crossed=bundle;crossed["serviceId"]="foreign";
    bool rejected=false;try{evaluateKnowledge(context,history,json::array(),[&](const std::string&,const std::string&){return crossed;});}catch(...){rejected=true;}
    check(rejected,"cross service accepted");
    rejected=false;try{evaluateKnowledge(context,history,json::array(),[](const std::string&,const std::string&) -> json {
      throw std::runtime_error("retrieval unavailable");
    });}catch(...){rejected=true;}
    check(rejected,"retrieval outage disguised as unknown");
    const auto candidate=json::array({{{"round",1},{"field","professional"},{"originalQuote","具体适用情况需要医生评估。"}}});
    check(run("具体适用情况需要医生评估。",candidate)["knowledgeChecks"][0]["verdict"]=="supported","professional exact evidence");
    json report={{"totalScore",100},{"passed",true},{"dimensionScores",{{"knowledgeAccuracy",100},{"medicalCompliance",80},
        {"empathy",80},{"needsDiscovery",80},{"serviceEtiquette",80}}}};
    const auto unscored=applyKnowledgeScores(report,none);
    check(unscored["totalScore"].is_null() && unscored["passed"].is_null() &&
        unscored["dimensionScores"]["knowledgeAccuracy"].is_null(),"null scores reweighted");
    check(applyKnowledgeScores(report,bad)["totalScore"]==60,"fixed weights wrong");
    auto ranged=price;ranged["type"]="range";ranged["minimumMinor"]=398000;ranged["maximumMinor"]=598000;
    check(compareKnowledgeFact("price","3980—5980元每颗，检查后确认",ranged)=="supported","price range");
    check(compareKnowledgeFact("price","预计5000元每颗",ranged)=="incomplete","within range estimate");
    auto hours=duration;hours["unit"]="minute";hours["minimum"]=60;hours["maximum"]=60;hours["conditions"]="";hours["estimated"]=false;
    check(compareKnowledgeFact("visitDuration","单次就诊1小时",hours)=="supported","hour minute normalization");
    check(compareKnowledgeFact("visitDuration","费用3980元，单次就诊1小时",hours)=="supported","unrelated numeric claim contaminated time");
    int fixed_passed=0;
    for (const auto& fixture : fixedKnowledgeCases()) {
      const auto quote=fixture.at("quote").get<std::string>();
      auto evidence=bundle;
      const auto mode=fixture.value("mode", "normal");
      if(mode=="unknown") {evidence["facts"]=json::array(); evidence["passages"]=json::array();}
      if(mode=="conflict") evidence["conflicts"]=json::array({"conflict"});
      if(mode=="cross") evidence["serviceId"]="other-service";
      json candidates=json::array();
      if(fixture.contains("field")) candidates.push_back({{"round",1},{"field",fixture["field"]},{"originalQuote",quote}});
      bool rejected_case=false; json actual;
      try {
        actual=evaluateKnowledge(context,json::array({{{"role","user"},{"round",1},{"content",quote}}}),candidates,
            [&](const std::string&,const std::string&){return evidence;});
      } catch(const std::runtime_error&) { rejected_case=true; }
      if(fixture["expected"]=="rejected") {
        check(rejected_case,"cross-service fixture accepted");
      } else {
        if(rejected_case || actual["knowledgeChecks"].empty() || actual["knowledgeChecks"][0]["verdict"]!=fixture["expected"])
          throw std::runtime_error("fixed case failed: "+fixture.dump()+" actual="+actual.dump());
      }
      ++fixed_passed;
    }
    auto replayable=bad; replayable["claimCandidates"]=json::array();
    const auto replay=replayKnowledgeAssessment(context,history,replayable);
    const auto prepared=prepareKnowledgeReport(report,replay,context);
    check(prepared["learningMistakes"].size()==1,"verified mistake missing");
    check(prepareKnowledgeReport(report,none,context)["learningMistakes"].empty(),"unknown became a mistake");
    check(prepareKnowledgeReport(report,corrected,context)["learningMistakes"].empty(),"corrected mistake retained");
    auto tampered=replayable; tampered["knowledgeAssessment"]["knowledgeAccuracy"]=100;
    bool tamper_rejected=false;
    try{replayKnowledgeAssessment(context,history,tampered);}catch(...){tamper_rejected=true;}
    check(tamper_rejected,"report score tampering accepted");
    std::cout<<"fixed knowledge cases passed: "<<fixed_passed<<"/"<<fixedKnowledgeCases().size()<<"\n";
    std::cout<<"knowledge evaluator tests passed: "<<checks<<" checks\n";
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
