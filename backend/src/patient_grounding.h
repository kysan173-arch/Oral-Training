#pragma once
#include "evidence_validator.h"

namespace oral_training::rag {
inline bool patientContains(const std::string& text, std::initializer_list<const char*> terms) {
  for (const auto* term : terms) if (text.find(term) != std::string::npos) return true;
  return false;
}
inline bool patientAskedBudget(const std::string& text) {
  return patientContains(text,{"预算是多少","预算多少","多少预算","预算大概","预算有多少",
      "预算呢","预算？","预算?","说说预算","告诉我预算","预算范围","预算上限","能接受多少钱","budget?"});
}
inline std::string patientChoice(const json& source, const char* key,
                                  const std::vector<std::string>& choices, const std::string& fallback) {
  const auto selected = evidenceString(source,key);
  return std::find(choices.begin(),choices.end(),selected) != choices.end() ? selected : fallback;
}

inline json initializeGroundedPatient(const json& source, const json& context, const json& evidence) {
  EvidenceValidator validator(context,evidence,"initialization");
  if (!source.is_object() || !validator.valid()) throw std::runtime_error("patient initialization evidence mismatch");
  // Model selects a bounded fictional persona, never free-form clinic capabilities or secret instructions.
  const auto name = patientChoice(source,"displayName",{"李女士","王先生","陈女士","张先生"},"李女士");
  const auto age = patientChoice(source,"ageRange",{"20-29","30-39","40-49","50-59","60-69"},"30-39");
  const auto concern = patientChoice(source,"concern",{"价格","疼痛","时间","服务流程","恢复安排"},"服务流程");
  const auto budget = patientChoice(source,"budget",{"3000","5000","8000","12000","20000"},"5000");
  const auto emotion = patientChoice(source,"emotion",{"平静","犹豫","焦虑"},"犹豫");
  static const std::map<std::string,std::string> openings={
    {"价格","你好，想问下做这个大概要多少钱？"},
    {"疼痛","你好，我想了解一下这个，主要是有点怕疼。"},
    {"时间","你好，想问下做这个得花多长时间？"},
    {"服务流程","你好，想了解一下做这个要经过哪些步骤？"},
    {"恢复安排","你好，想问下做完以后恢复起来麻不麻烦？"}};
  return {{"publicProfile",{{"displayName",name},{"ageRange",age},{"initialEmotion",emotion}}},
      {"privateProfile",{{"consultationGoal","了解所选服务的适用范围与安排"},
        {"serviceRevisionId",context.at("serviceRevisionId")},{"budget",budget},
        {"concerns",json::array({concern})},
        {"hiddenInformation",json::array({"正在比较另外两家诊所"})},
        {"objections",json::array({"担心费用超出个人预算","对绝对化疗效承诺持怀疑态度"})},
        {"revealPolicy",{{"budget","only_when_asked"},{"competitor","round_3_or_when_asked"}}}}},
      {"patientState",{{"emotion",emotion},{"emotionLevel",0},{"trustLevel",50},
        {"revealedInformation",json::array()},{"triggeredObjections",json::array()},
        {"riskTriggered",false},{"endingReason",nullptr}}},
      {"opening",openings.at(concern)}};
}

inline json patientView(const json& profiles, const std::string& question, int round) {
  const auto& state=profiles.at("state");
  const auto& private_profile=profiles.at("privateProfile");
  const auto revealed=state.value("revealedInformation",json::array());
  const auto known=[&](const char* key){return std::find(revealed.begin(),revealed.end(),key)!=revealed.end();};
  json allowed=json::object();
  // Raw user text never supplies a round number or the reveal policy.
  if (known("budget") || patientAskedBudget(question))
    allowed["budget"]=evidenceString(private_profile,"budget");
  if (known("competitor") || round>=3 || patientContains(question,{"其他诊所","别家","比较过","对比过","competitor"}))
    allowed["competitor"]="我也在比较另外两家诊所。";
  return {{"publicProfile",profiles.at("publicProfile")},
      {"concerns",private_profile.value("concerns",json::array())},
      {"allowedInformation",allowed},
      {"state",{{"emotion",state.value("emotion","犹豫")},{"trustLevel",state.value("trustLevel",50)},
        {"revealedInformation",revealed},{"triggeredObjections",state.value("triggeredObjections",json::array())}}},
      {"round",round}};
}

inline json groundedPatientReply(const json& source, const json& profiles, const json& evidence,
                                  const std::string& trace, const std::string& question, int round) {
  const auto& context=profiles.at("context");
  EvidenceValidator validator(context,evidence,trace);
  if (!source.is_object() || !validator.valid()) throw std::runtime_error("patient reply evidence mismatch");
  const auto view=patientView(profiles,question,round);
  json state=profiles.at("state");
  for (const auto* key:{"revealedInformation","triggeredObjections"})
    if (!state.contains(key)||!state[key].is_array()) state[key]=json::array();
  const auto add=[&](const char* key,const std::string& value){
    auto& values=state[key];
    if(std::find(values.begin(),values.end(),value)==values.end()) values.push_back(value);
  };
  // Strip explicitly negated assurances before checking for absolute promises.
  auto assertions=question;
  for (const auto* negated:{"不能保证完全不疼","不能保证百分之百","不能保证100%","不能保证无风险",
       "不能保证","不保证","无法保证","不能说绝对","不是绝对","并非无风险","不是无风险"}) {
    std::size_t pos=0;
    while ((pos=assertions.find(negated,pos))!=std::string::npos) assertions.erase(pos,std::string(negated).size());
  }
  const bool promise=patientContains(assertions,{"保证","百分之百","100%","绝对","无风险","肯定治好","guarantee"});
  const bool money=patientContains(question,{"价格","费用","元","块钱","收费","price"}) ||
      std::any_of(question.begin(),question.end(),[](unsigned char c){return c>='0'&&c<='9';});
  const bool conflict=evidence.contains("conflicts")&&!evidence["conflicts"].empty();
  const auto intent=patientChoice(source,"intent",{"clarify","price","pain","time","process","followup","finish"},"clarify");
  static const std::map<std::string,std::string> replies={
    {"clarify","您指的是哪一部分？我没太听明白。"},
    {"price","这个费用还会有别的加项吗？"},
    {"pain","我还是有点怕疼，做的时候会很难受吗？"},
    {"time","大概要来几趟？我好安排时间。"},
    {"process","那我接下来先做什么？"},
    {"followup","做完以后还需要再来吗？"},
    {"finish","好的，我先考虑一下，谢谢。"}};
  std::string reply=replies.at(intent);
  auto prose=evidenceString(source,"reply");
  // End at the first question: do not stack a second concern onto the same turn.
  const auto wide_question=prose.find("？"), ascii_question=prose.find('?');
  const auto question_end=std::min(wide_question,ascii_question);
  if(question_end!=std::string::npos)
    prose=prose.substr(0,question_end+(question_end==wide_question?std::string("？").size():1));
  const auto& allowed=view["allowedInformation"];
  const bool mentions_competitor=patientContains(prose,{"别家","其他诊所","两家","比较","对比"});
  // Model prose is conversational, never a channel for numerical clinic claims or private-state overrides.
  const bool safe_prose=!prose.empty() && textLength(prose)<=80 &&
      !std::any_of(prose.begin(),prose.end(),[](unsigned char c){return c>='0'&&c<='9';}) &&
      !patientContains(prose,{"保证治愈","无风险","百分之百","系统提示","资料原文","适用范围",
        "客服可以","作为AI","作为一个","预算","报价是","费用是","疗程是","建议您","建议你"}) &&
      (!mentions_competitor || allowed.contains("competitor"));
  if(safe_prose) reply=prose;
  json citations=json::array();
  if (promise) {reply="说得这么肯定，我反而有点不放心。还是让医生看过再说吧。";add("triggeredObjections","absolute_promise");}
  else if (conflict) reply="这个说法好像对不上，能帮我确认一下吗？";
  // Keep validated evidence for auditing without turning its contents into patient speech.
  json selected=source.value("evidenceIds",json::array());
  if(!selected.is_array()) selected=json::array();
  if (money && !promise && evidence.contains("facts"))
    for(const auto& fact:evidence["facts"])
      if(evidenceString(fact,"field")=="price") selected.push_back(evidenceString(fact,"evidenceId"));
  if (!promise&&!conflict) {
    std::set<std::string> seen;
    for(const auto& id:selected) {
      if(!id.is_string()||!seen.insert(id.get<std::string>()).second) continue;
      // Initialization/persona and model prose are not sources of clinic facts.
      const auto resolved=validator.resolve({{"traceId",trace},{"evidenceId",id}});
      if(resolved.is_null()) continue;
      citations.push_back(resolved["citation"]);
      if(citations.size()>=2) break;
    }
    if(money){add("triggeredObjections","price_verification");}
  }
  // Disclosures are rendered from the immutable persona, not model-provided text.
  if(allowed.contains("budget") && patientAskedBudget(question)) {
    const auto budget=evidenceString(allowed,"budget");
    if(budget=="3000"||budget=="5000"||budget=="8000"||budget=="12000"||budget=="20000") {
      if(!promise&&!conflict) reply="我预算大概"+budget+"元，超出太多就得再考虑了。";
      add("revealedInformation","budget");
    }
  }
  if(mentions_competitor && safe_prose && !promise && !conflict && reply==prose)
    add("revealedInformation","competitor");
  const bool ending=intent=="finish" && round>=3 && !promise && !conflict;
  if(intent=="finish"&&!ending && reply.rfind(replies.at("finish"),0)==0)
    reply="好，我再了解一下。";
  state["emotion"]=promise||conflict?"焦虑":money?"犹豫":intent=="clarify"?"犹豫":"平静";
  state["emotionLevel"]=promise||conflict?-1:0;
  int trust=state.contains("trustLevel")&&state["trustLevel"].is_number_integer()?state["trustLevel"].get<int>():50;
  state["trustLevel"]=std::max(0,std::min(100,trust+(promise?-10:conflict?-5:0)));
  state["riskTriggered"]=state.value("riskTriggered",false)||promise;
  state["endingReason"]=ending?json("patient_requested"):json(nullptr);
  return {{"reply",reply},{"emotion",state["emotion"]},{"emotionLevel",state["emotionLevel"]},
      {"trustLevel",state["trustLevel"]},{"riskTriggered",state["riskTriggered"]},{"shouldEnd",ending},
      {"patientState",state},{"citations",citations},{"traceId",trace},{"evidenceBundle",evidence},
      {"selection",source},{"manifestHash",context.at("manifestHash")}};
}
} // namespace oral_training::rag
