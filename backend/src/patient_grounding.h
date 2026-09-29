#pragma once
#include "evidence_validator.h"

namespace oral_training::rag {
inline bool patientContains(const std::string& text, std::initializer_list<const char*> terms) {
  for (const auto* term : terms) if (text.find(term) != std::string::npos) return true;
  return false;
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
      {"opening","您好，我想咨询这项服务，比较关心"+concern+"，能先帮我说明一下吗？"}};
}

inline json patientView(const json& profiles, const std::string& question, int round) {
  const auto& state=profiles.at("state");
  const auto& private_profile=profiles.at("privateProfile");
  const auto revealed=state.value("revealedInformation",json::array());
  const auto known=[&](const char* key){return std::find(revealed.begin(),revealed.end(),key)!=revealed.end();};
  json allowed=json::object();
  // Raw user text never supplies a round number or the reveal policy.
  if (known("budget") || patientContains(question,{"预算","预算多少","能接受多少钱","budget"}))
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
  const bool promise=patientContains(question,{"保证","百分之百","100%","绝对","无风险","肯定治好","guarantee"});
  const bool money=patientContains(question,{"价格","费用","元","块钱","收费","price"}) ||
      std::any_of(question.begin(),question.end(),[](unsigned char c){return c>='0'&&c<='9';});
  const bool conflict=evidence.contains("conflicts")&&!evidence["conflicts"].empty();
  const auto intent=patientChoice(source,"intent",{"clarify","price","pain","time","process","followup","finish"},"clarify");
  static const std::map<std::string,std::string> replies={
    {"clarify","我还不太明白，能结合我刚才的问题具体解释一下吗？"},
    {"price","我比较在意费用，能说明计价单位、包含项目和适用条件吗？"},
    {"pain","我有些担心不适，能说明哪些需要先由医生检查评估吗？"},
    {"time","我需要安排时间，能说明就诊时间和整个治疗周期的区别吗？"},
    {"process","如果我考虑这项服务，下一步需要先了解哪些条件？"},
    {"followup","后续有哪些安排需要进一步确认？"},
    {"finish","谢谢您的说明，我会先考虑，并通过面诊进一步确认适用情况。"}};
  std::string reply=replies.at(intent);
  json citations=json::array();
  if (promise) {reply="您能保证完全没有风险吗？我希望了解适用条件和不确定性，具体效果还是需要医生评估。";add("triggeredObjections","absolute_promise");}
  else if (conflict) reply="资料里的说法似乎有冲突，能先帮我核实清楚，再说明适用条件吗？";
  // Claims trigger verification; only validated locked facts or passages may be echoed.
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
      const auto text=resolved["text"].get<std::string>();
      if(textLength(reply)+textLength(text)>600) continue;
      reply+=" 我看到的资料写着「"+text+"」，这和您刚才的说明一致吗？";
      citations.push_back(resolved["citation"]);
      if(citations.size()>=2) break;
    }
    if(money){add("triggeredObjections","price_verification");}
    if(citations.empty()) reply+=" 我还没有看到可以核实的资料，能帮我确认一下吗？";
    else if(evidence.contains("missingFields")&&!evidence["missingFields"].empty())
      reply+=" 资料没写明的部分，也请帮我进一步确认。";
  }
  const auto& allowed=view["allowedInformation"];
  // Disclosures are rendered from the immutable persona, not model-provided text.
  if(allowed.contains("budget") && patientContains(question,{"预算","能接受多少钱","budget"})) {
    const auto budget=evidenceString(allowed,"budget");
    if(budget=="3000"||budget=="5000"||budget=="8000"||budget=="12000"||budget=="20000") {
      reply+=" 我个人暂时考虑的预算是"+budget+"元左右，这只是我的预算，不是诊所报价。";
      add("revealedInformation","budget");
    }
  }
  if(allowed.contains("competitor")&&(round>=3||patientContains(question,{"其他诊所","别家","比较过","对比过","competitor"}))) {
    reply+=" 我也在比较另外两家诊所，希望先把条件弄清楚。";add("revealedInformation","competitor");
  }
  const bool ending=intent=="finish" && round>=3 && !promise && !money && !conflict && !citations.empty();
  if(intent=="finish"&&!ending && reply.rfind(replies.at("finish"),0)==0)
    reply="我还有些信息想核实清楚。"+reply.substr(replies.at("finish").size());
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
