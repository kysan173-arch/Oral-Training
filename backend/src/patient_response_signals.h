#pragma once
#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace oral_training::rag {
inline bool patientContains(const std::string& text, std::initializer_list<const char*> terms) {
  for (const auto* term : terms) if (text.find(term) != std::string::npos) return true;
  return false;
}

// Denials and rejected third-party quotes are not fresh promises.
inline bool patientAbsolutePromise(std::string text) {
  const auto quote_start=text.find("“"), quote_end=text.find("”");
  if(quote_start!=std::string::npos && quote_end>quote_start && quote_end!=std::string::npos &&
     patientContains(text.substr(0,quote_start),{"别家","其他诊所","有人","网上"}) &&
     patientContains(text.substr(quote_end),{"不可信","不要相信","不能相信","不认同","不认可","不作这种承诺"}))
    text.erase(quote_start,quote_end+std::string("”").size()-quote_start);
  for(const auto* boundary:{"。","！","？","，","；",",",";","但是","不过","然而","但","而且"}) {
    size_t pos=0;
    while((pos=text.find(boundary,pos))!=std::string::npos) {
      text.replace(pos,std::string(boundary).size(),"\n"); ++pos;
    }
  }
  std::istringstream clauses(text);
  std::string clause;
  while(std::getline(clauses,clause)) {
    const bool double_negative=patientContains(clause,{"不是不能","并非不能","不是不保证","并非不保证"});
    if(!double_negative && patientContains(clause,{"不能保证","无法保证","没法保证","不保证","不会保证",
        "不作保证","不做保证","无法承诺","不能承诺","不承诺","不能说绝对","不是绝对","并非无风险","不是无风险"})) continue;
    if(patientContains(clause,{"保证","百分之百","100%","绝对","无风险","肯定治好","guarantee"})) return true;
  }
  return false;
}

inline bool patientDeflectingResponse(const std::string& text) {
  return patientContains(text,{"不知道","您先说","您问","您请说","麻烦您说","我不太懂",
      "您继续","您说说","自己看","自己查","别问","别烦","没空","不关我","懒得"});
}

// A topic chosen by the patient model is not evidence of learner empathy.
inline bool patientSupportiveResponse(const std::string& text) {
  return !patientAbsolutePromise(text) && !patientDeflectingResponse(text) &&
      patientContains(text,{"理解您","理解你","了解您的担心","了解您的顾虑","体会您","抱歉"}) &&
      patientContains(text,{"帮您","为您","陪您","一起","先了解","先听","可以告诉我","请告诉我"});
}

// These are explicit non-numeric deferrals, not factual price claims. Share the
// recognition between live patient turns and the end-of-session assessment.
inline std::vector<std::string> patientDeferredPriceTopics(const std::string& text) {
  if(patientAbsolutePromise(text) || patientDeflectingResponse(text) ||
      patientContains(text,{"？","?","如果","假如","别家","其他诊所","您说","听说",
        "不会核实","不核实","不会回复","不回复","不再回复"}) ||
      std::any_of(text.begin(),text.end(),[](unsigned char c){return c>='0' && c<='9';}) ||
      patientContains(text,{"十","百","千","万","零","一元","二元","两元","三元","四元","五元","六元","七元","八元","九元"}) ||
      !patientContains(text,{"核实后回复","确认后回复","稍后回复","回头回复","回头说明",
        "具体费用以","以实际为准","以面诊为准"})) return {};
  std::vector<std::string> topics;
  if(patientContains(text,{"价格","费用","报价"})) topics.push_back("price");
  if(patientContains(text,{"包含","包括","另收","不含"})) topics.push_back("includedItems");
  return topics;
}
} // namespace oral_training::rag
