#pragma once
#include "evidence_validator.h"
#include <cmath>
#include <functional>
#include <regex>

namespace oral_training::rag {
// The provider must retrieve from the supplied locked context, outside a write transaction.
using KnowledgeEvidenceProvider = std::function<json(const std::string&, const std::string&)>;
inline bool claimHas(const std::string& text, std::initializer_list<const char*> words) {
  for (const auto* word : words) if (text.find(word) != std::string::npos) return true;
  return false;
}
inline const std::vector<std::string>& knowledgeFields() {
  static const std::vector<std::string> fields = {
      "price", "includedItems", "visitDuration", "treatmentDuration", "followupInterval", "appointment", "professional"};
  return fields;
}
inline std::vector<std::string> claimFields(const std::string& text) {
  std::vector<std::string> result;
  if (claimHas(text,{"元","价格","费用","报价"})) result.push_back("price");
  if (claimHas(text,{"包含","包括","另收","不含"})) result.push_back("includedItems");
  if (claimHas(text,{"就诊","单次","每次"}) && claimHas(text,{"分钟","小时","多久"})) result.push_back("visitDuration");
  if (claimHas(text,{"疗程","完成治疗","治疗周期"}) ||
      (claimHas(text,{"个月"}) && !claimHas(text,{"复诊","复查"}))) result.push_back("treatmentDuration");
  if (claimHas(text,{"复诊","复查"}) && claimHas(text,{"间隔","多久","天","周","个月","年"})) result.push_back("followupInterval");
  if (claimHas(text,{"预约","号源","锁定","挂号"})) result.push_back("appointment");
  return result;
}
inline bool claimExcluded(const std::string& text) {
  return claimHas(text,{"？","?","吗","多少钱","多久","是否","如果","假如","假设","比如",
      "别家","其他诊所","您说","患者说","听说","转述","预算","不是","并非","不代表","不收","无需支付","不需要",
      "不能保证","无法保证","不保证"});
}
struct ClaimSpan { std::string text; std::size_t offset; };
inline std::vector<ClaimSpan> claimSpans(const std::string& text) {
  // Keep comma-linked conditions/negations together. Offsets are UTF-8 byte offsets, not UI character indices.
  static const std::regex stop("。|！|!|？|\\?|；|;|\\n");
  std::vector<ClaimSpan> spans;
  std::size_t begin=0;
  for (std::sregex_iterator it(text.begin(),text.end(),stop),end;it!=end;++it) {
    const auto finish=static_cast<std::size_t>(it->position()+it->length());
    if(finish>begin) spans.push_back({text.substr(begin,finish-begin),begin});
    begin=finish;
  }
  if(begin<text.size()) spans.push_back({text.substr(begin),begin});
  return spans;
}
inline std::vector<double> claimNumbers(const std::string& text) {
  static const std::regex number("[0-9]+(?:\\.[0-9]{1,2})?");
  std::vector<double> values;
  for(std::sregex_iterator it(text.begin(),text.end(),number),end;it!=end;++it) {
    if(it->length()>12) return {};
    values.push_back(std::stod(it->str()));
  }
  return values;
}
inline std::string compareKnowledgeFact(const std::string& field,const std::string& quote,const json& value) {
  auto numbers=claimNumbers(quote);
  if(field=="price") {
    numbers.clear();
    static const std::regex amount("([0-9]+(?:\\.[0-9]{1,2})?)(?:\\s*(?:—|-|至|到|~)\\s*([0-9]+(?:\\.[0-9]{1,2})?))?\\s*元");
    for(std::sregex_iterator it(quote.begin(),quote.end(),amount),end;it!=end;++it) {
      if((*it)[1].length()>12 || (*it)[2].length()>12) return "evidence_missing";
      numbers.push_back(std::stod((*it)[1]));
      if((*it)[2].matched) numbers.push_back(std::stod((*it)[2]));
    }
  }
  const bool absolute=claimHas(quote,{"一定","保证","肯定","绝对","没有其他条件","无条件"});
  if(field=="appointment") {
    if(claimHas(quote,{"锁定","已经预约","已预约","保证有号"})) return "contradicted";
    return claimHas(quote,{"需要确认","以预约确认为准","非实时","帮您确认"}) ? "supported" : "incomplete";
  }
  if(field=="includedItems") {
    bool mentioned=false,missing=false;
    for(const auto& item:value) {
      if(quote.find(item.get<std::string>())!=std::string::npos) {
        mentioned=true;
        if(claimHas(quote,{"不含","不包含","另收"})) return "contradicted";
      } else missing=true;
    }
    if(missing) return "incomplete";
    // Unknown additional inclusions are not certified by a subset match.
    std::string remainder=quote;
    for(const auto& item:value) {
      const auto word=item.get<std::string>();
      for(auto pos=remainder.find(word);pos!=std::string::npos;pos=remainder.find(word)) remainder.erase(pos,word.size());
    }
    for(const auto* word:{"包含项目","包含","包括","和","、","，",",","。","；"," ","：",":"})
      for(auto pos=remainder.find(word);pos!=std::string::npos;pos=remainder.find(word)) remainder.erase(pos,std::string(word).size());
    return mentioned && remainder.empty() ? "supported" : "evidence_missing";
  }
  if(claimHas(quote,{"需要医生","医生检查","医生评估","检查后评估"}) && numbers.empty() && !absolute)
    return "not_applicable";
  if(field=="price") {
    const auto type=evidenceString(value,"type");
    if(type=="starting_from" && claimHas(quote,{"封顶","不超过","最多","上限"})) return "contradicted";
    if(type=="quote_after_assessment") return numbers.empty() ? "supported" : "contradicted";
    if(numbers.empty()) return "evidence_missing"; // Never infer unsupported Chinese-number semantics.
    if(claimHas(quote,{"美元","港币","美金","万元","万块"})) return "evidence_missing";
    bool matching=false;
    if(type=="range") {
      matching=numbers.size()==2 && std::llround(numbers[0]*100)==value.at("minimumMinor").get<long long>() &&
          std::llround(numbers[1]*100)==value.at("maximumMinor").get<long long>();
      if(numbers.size()==1 && claimHas(quote,{"预计","估计","大约"}) && numbers[0]*100>=value.at("minimumMinor").get<double>() &&
          numbers[0]*100<=value.at("maximumMinor").get<double>()) return "incomplete";
    } else matching=numbers.size()==1 && std::llround(numbers[0]*100)==value.at("amountMinor").get<long long>();
    if(!matching) return "contradicted";
    const std::map<std::string,std::vector<std::string>> units={
      {"per_tooth",{"每颗","一颗","/颗"}},{"per_case",{"每例","一例","/例"}},
      {"per_visit",{"每次","一次","/次"}},{"per_arch",{"每牙弓","/牙弓"}},{"per_item",{"每项","一项","/项"}}};
    const auto expected=evidenceString(value,"unit");
    bool unit_ok=false;
    for(const auto& [unit,terms]:units) for(const auto& term:terms) if(quote.find(term)!=std::string::npos) {
      if(unit!=expected) return "contradicted";
      unit_ok=true;
    }
    if(absolute && !evidenceString(value,"conditions").empty()) return "contradicted";
    if(!unit_ok || (type=="starting_from" && quote.find("起")==std::string::npos)) return "incomplete";
    const auto condition=evidenceString(value,"conditions");
    if(!condition.empty() && quote.find(condition)==std::string::npos) return "incomplete";
    for(const auto* key:{"validFrom","validUntil"}) {
      const auto date=evidenceString(value,key);
      if(!date.empty() && quote.find(date)==std::string::npos) return "incomplete";
    }
    return "supported";
  }
  // Time: distinct fields and units; month is deliberately not equated to a fixed count of days.
  if(absolute && (value.value("estimated",false)||!evidenceString(value,"conditions").empty())) return "contradicted";
  numbers.clear();
  static const std::regex time_amount("([0-9]+(?:\\.[0-9]{1,2})?)(?:\\s*(?:—|-|至|到|~)\\s*([0-9]+(?:\\.[0-9]{1,2})?))?\\s*(分钟|小时|个月|天|周|年)");
  std::string parsed_unit;
  int time_groups=0;
  for(std::sregex_iterator it(quote.begin(),quote.end(),time_amount),end;it!=end;++it) {
    if(++time_groups>1 || (*it)[1].length()>12 || (*it)[2].length()>12) return "evidence_missing";
    numbers.push_back(std::stod((*it)[1]));
    if((*it)[2].matched) numbers.push_back(std::stod((*it)[2]));
    parsed_unit=(*it)[3];
  }
  if(numbers.empty()) return "evidence_missing";
  const std::map<std::string,std::string> units={{"minute","分钟"},{"hour","小时"},{"day","天"},
      {"week","周"},{"month","个月"},{"year","年"}};
  std::string unit;
  for(const auto& [key,label]:units) if(parsed_unit==label) unit=key;
  if(unit.empty()) return "incomplete";
  double factor=1;
  const auto expected=evidenceString(value,"unit");
  if(unit!=expected) {
    if(unit=="hour" && expected=="minute") factor=60;
    else if(unit=="minute" && expected=="hour") factor=1.0/60;
    else return "contradicted";
  }
  const double low=value.at("minimum").get<double>(),high=value.at("maximum").get<double>();
  if(numbers.size()>2 || std::abs(numbers[0]*factor-low)>0.00001 ||
      std::abs(numbers.back()*factor-high)>0.00001) return "contradicted";
  if(value.value("estimated",false) && !claimHas(quote,{"约","预计","估计"})) return "incomplete";
  for(const auto* key:{"conditions","phase"}) {
    const auto condition=evidenceString(value,key);
    if(!condition.empty() && quote.find(condition)==std::string::npos) return "incomplete";
  }
  return "supported";
}

// Model candidates only identify an existing source span and a field. Scores, verdicts, numeric
// values and offsets supplied by the model are ignored. Structured fields also have a server scan.
inline json evaluateKnowledge(const json& context,const json& history,const json& candidates,
                              const KnowledgeEvidenceProvider& retrieve) {
  if(!history.is_array() || !candidates.is_array() || candidates.size()>100)
    throw std::runtime_error("invalid knowledge extraction");
  json checks=json::array(),traces=json::array();
  std::map<std::string,std::size_t> units;
  std::map<std::string,int> asked;
  std::map<std::string,std::string> asked_quotes;
  std::set<std::string> deferred_to_doctor;
  // 已被「会核实后回复」这类合规应答处理过的价格类字段。
  // 与 deferred_to_doctor 分开：那个是医疗判断的转交，这个是「价格问过了、
  // 对方已承诺回复」，用于让患者侧停止重复追问同一诉求。
  std::set<std::string> deferred_price_topics;
  int last_user_round=0;
  std::vector<json> ordered(history.begin(),history.end());
  std::stable_sort(ordered.begin(),ordered.end(),[](const json& a,const json& b){return a.value("round",0)<b.value("round",0);});
  for(const auto& message:ordered) {
    const auto role=evidenceString(message,"role"),content=evidenceString(message,"content");
    const int round=message.value("round",0);
    if(role=="patient") {
      for(const auto& field:claimFields(content)) if(claimHas(content,{"?","？","多少","多久","能否","吗"})) {
        asked[field]=round; asked_quotes[field]=content;
      }
      continue;
    }
    if(role!="user" || round<1 || round>10) continue;
    last_user_round=std::max(last_user_round,round);
    if(claimHas(content,{"需要医生评估","医生检查后评估"}) && claimNumbers(content).empty() &&
        !claimHas(content,{"一定","保证","绝对"})) {
      for(const auto& [field,asked_round]:asked)
        if(asked_round<round && (field=="treatmentDuration" || field=="visitDuration" || field=="followupInterval"))
          deferred_to_doctor.insert(field);
    }
    for(const auto& span:claimSpans(content)) {
      auto fields=claimFields(span.text);
      for(const auto& candidate:candidates) {
        if(!candidate.is_object() || candidate.value("round",0)!=round) continue;
        const auto quote=evidenceString(candidate,"originalQuote"),field=evidenceString(candidate,"field");
        if(quote.empty() || span.text.find(quote)==std::string::npos ||
            std::find(knowledgeFields().begin(),knowledgeFields().end(),field)==knowledgeFields().end()) continue;
        // Candidate topics may not introduce arbitrary scored units.
        if(field=="professional" || std::find(fields.begin(),fields.end(),field)!=fields.end()) fields.push_back(field);
      }
      std::sort(fields.begin(),fields.end()); fields.erase(std::unique(fields.begin(),fields.end()),fields.end());
      for(const auto& field:fields) {
        if(traces.size()>=200) throw std::runtime_error("too many knowledge claims");
        const auto trace="kc-trace-"+std::to_string(traces.size()+1);
        const auto evidence=retrieve(field,span.text);
        EvidenceValidator validator(context,evidence,trace);
        if(!validator.valid()) throw std::runtime_error("knowledge evidence context mismatch");
        traces.push_back({{"traceId",trace},{"evidence",evidence}});
        std::string verdict="evidence_missing",reason="现有证据或确定性解析不足，不计入知识分";
        json refs=json::array(),expected=nullptr;
        const auto contrast=span.text.find("而是");
        const bool explicit_contrast=span.text.find("不是")<contrast && contrast!=std::string::npos &&
            !claimHas(span.text,{"如果","假如","假设","比如","别家","其他诊所","您说","患者说","听说","转述","预算"});
        const std::string asserted=explicit_contrast?span.text.substr(contrast+std::string("而是").size()):span.text;
        const bool correction=explicit_contrast || claimHas(span.text,{"更正","纠正","刚才说错","应为","准确说"});
        if(claimExcluded(asserted)) verdict="not_applicable";
        else if(evidence.contains("conflicts") && !evidence["conflicts"].empty()) verdict="conflicted";
        else {
          std::vector<json> facts;
          for(const auto& fact:evidence["facts"]) if(evidenceString(fact,"field")==field) {
            const auto resolved=validator.resolve({{"traceId",trace},{"evidenceId",evidenceString(fact,"evidenceId")}});
            if(!resolved.is_null()) { facts.push_back(fact); refs.push_back(resolved["citation"]); }
          }
          if(facts.size()>1) verdict="conflicted";
          else if(facts.size()==1) {
            expected=facts[0]["value"];
            verdict=compareKnowledgeFact(field,asserted,expected);
          } else if(field=="professional") {
            for(const auto& passage:evidence["passages"]) {
              const auto resolved=validator.resolve({{"traceId",trace},{"evidenceId",evidenceString(passage,"evidenceId")}});
              if(!resolved.is_null() && span.text==evidenceString(passage,"body")) {
                verdict="supported"; refs.push_back(resolved["citation"]); expected=passage["body"]; break;
              }
            }
          }
        }
        if(verdict=="supported") reason="陈述与锁定证据及其条件一致";
        if(verdict=="contradicted") reason="陈述的值、单位或承诺与锁定证据不一致；知识核验不替代医疗合规评价";
        if(verdict=="incomplete") reason="陈述遗漏范围、单位或适用条件";
        if(verdict=="not_applicable") reason="疑问、假设、否定或转述，不作为本诊所事实承诺计分";
        if(verdict=="conflicted") reason="证据存在冲突，暂不计分";
        // 合规地不报价：明确说明会核实后回复、或把话题转到「费用包含什么」，
        // 且没有说出任何被资料否定的数值。这类应答此前落进 evidence_missing
        // 或 incomplete，既不计分也不告诉患者「这个话题已经回应过」，
        // 于是患者每轮都回来追问。判为 deferred：给中性 credit，
        // 并写入 deferredTopics 让患者侧收敛。
        // 两种起点都要覆盖：「价格我稍后回复」是 evidence_missing，
        // 「费用包含哪些项目我回头说明」因为「包含」被识别成 includedItems
        // 而判 incomplete —— 两者都是「承诺稍后答」而非「答错了」。
        // 注意关键词必须精确：早期版本含「帮您确认」，但「预约我帮您确认」是
        // 合规且正确的应答（既有夹具期望 supported），会被误判。
        // 因此只保留「确认后回复」这类明确指向「稍后答复」的组合。
        if((verdict=="evidence_missing" || verdict=="incomplete") && claimHas(span.text,
            {"核实后回复","确认后回复","稍后回复","回头回复","回头说明",
             "具体费用以","以实际为准","以面诊为准"}))
          verdict="deferred";
        if(verdict=="deferred") {
          reason="已说明会核实后回复或转为费用构成说明，未编造数值；属合规应答，不因未给价扣分";
          if(field=="price"||field=="includedItems") deferred_price_topics.insert(field);
        }
        if(verdict=="not_applicable" && claimHas(span.text,{"需要医生","医生检查","医生评估","检查后评估"}))
          deferred_to_doctor.insert(field);
        json check={{"checkId","kc-"+std::to_string(checks.size()+1)},{"round",round},{"originalQuote",span.text},
          {"sourceOffsetBytes",span.offset},{"topic",field},{"verdict",verdict},{"reason",reason},
          {"evidenceRefs",refs},{"expectedFact",expected},{"severity",field=="price"||field=="includedItems"?"major":"minor"},
          {"scoreImpact",nullptr},{"correctedInLaterRound",false},{"scoringUnit",false}};
        const auto unit_key=field=="professional" ? field+":"+expected.dump() : field;
        check["unitKey"]=unit_key;
        const bool applicable=verdict!="not_applicable";
        if(applicable) {
          auto previous=units.find(unit_key);
          if(previous==units.end()) { units[unit_key]=checks.size(); check["scoringUnit"]=true; }
          else if((correction && (verdict=="supported" || verdict=="incomplete")) || verdict=="contradicted") {
            checks[previous->second]["scoringUnit"]=false;
            if(correction && verdict=="supported") for(auto& earlier:checks)
              if(evidenceString(earlier,"unitKey")==unit_key) earlier["correctedInLaterRound"]=true;
            check["scoringUnit"]=true; units[unit_key]=checks.size();
          }
        }
        checks.push_back(check);
      }
    }
  }
  // A question is not a learner claim. Missing answers retain the patient's actual source separately.
  for(const auto& [field,round]:asked) if(!units.count(field) && !deferred_to_doctor.count(field) && round<last_user_round) {
    const auto trace="kc-trace-"+std::to_string(traces.size()+1);
    const auto evidence=retrieve(field,field);
    EvidenceValidator validator(context,evidence,trace);
    if(!validator.valid()) throw std::runtime_error("question evidence context mismatch");
    traces.push_back({{"traceId",trace},{"evidence",evidence}});
    json refs=json::array();
    for(const auto& fact:evidence["facts"]) if(evidenceString(fact,"field")==field) {
      const auto ref=validator.resolve({{"traceId",trace},{"evidenceId",evidenceString(fact,"evidenceId")}});
      if(!ref.is_null()) refs.push_back(ref["citation"]);
    }
    if(refs.size()!=1 || (evidence.contains("conflicts")&&!evidence["conflicts"].empty())) continue;
    units[field]=checks.size();
    checks.push_back({{"checkId","kc-"+std::to_string(checks.size()+1)},{"round",round},{"originalQuote",""},
        {"sourceRole","patient"},{"patientQuestion",asked_quotes[field]},
        {"topic",field},{"verdict","incomplete"},{"reason","患者询问且资料可答，但未发现有效学员回答"},
        {"evidenceRefs",refs},{"expectedFact",nullptr},{"severity","minor"},{"scoreImpact",nullptr},
        {"correctedInLaterRound",false},{"scoringUnit",true},{"unanswered",true}});
  }
  int assessable=0,unassessable=0; double earned=0,weight_sum=0;
  for(const auto& [field,index]:units) {
    auto& check=checks[index]; const auto verdict=evidenceString(check,"verdict");
    if(verdict=="evidence_missing" || verdict=="conflicted") { ++unassessable; continue; }
    const bool clinical_risk=field.rfind("professional:",0)==0 &&
        check["expectedFact"].is_string() && claimHas(check["expectedFact"].get<std::string>(),{"风险","禁忌","急诊"});
    const double weight=field=="price"||field=="includedItems"||clinical_risk?2:1;
    // deferred 与 incomplete 同给 0.5：合规地承诺稍后回复是合格应答（不该拿 0 分，
    // 那等于惩罚正确行为），但确实没有陈述可核验的事实，所以不给满分。
    const double credit=verdict=="supported"?1:
        (verdict=="incomplete"&&!check.value("unanswered",false))||verdict=="deferred"?0.5:0;
    ++assessable; weight_sum+=weight; earned+=weight*credit;
    check["unitWeight"]=weight; check["unitCredit"]=credit;
  }
  for(auto& check:checks) if(check.contains("unitWeight"))
    check["scoreImpact"]=-100.0*check["unitWeight"].get<double>()*(1-check["unitCredit"].get<double>())/weight_sum;
  const json score=assessable?json(static_cast<int>(std::lround(100*earned/weight_sum))):json(nullptr);
  json deferred_topics=json::array();
  for(const auto& field:deferred_price_topics) deferred_topics.push_back(field);
  return {{"knowledgeChecks",checks},{"knowledgeTraces",traces},{"knowledgeManifestHash",context.at("manifestHash")},
      // 患者侧用它收敛话题：这些字段已被「会核实后回复」处理过，别再重复追问。
      {"deferredTopics",deferred_topics},
      {"knowledgeAssessment",{{"status",assessable?"partial":"insufficient_evidence"},{"knowledgeAccuracy",score},
        {"assessableCount",assessable},{"unassessableCount",unassessable},
        {"coverage",units.empty()?json(nullptr):json(static_cast<double>(assessable)/units.size())},
        {"rubricVersion","knowledge-rubric-v1"}}}};
}
// 信任度正向信号汇总。信任度每轮由 patient_grounding.h 结算，只按违规扣分
// （承诺-10 / 冲突 -5），没有任何正向边—— 学员说对话、答得准、共情到位，
// 数值一动不动。它衡量的是「没犯规几次」而不是「患者多信任你」，打几十场
// 曲线单边向下。这里在结算期把已有的确定性判定汇成一句可展示的评价：
// 不改每轮状态机，不碰 profiles 表，也不额外调模型。
inline json summarizeTrustBuilding(const json& assessment,const json& patient_state) {
  int accurate = 0, corrected = 0, deferred = 0;
  for (const auto& check : assessment.value("knowledgeChecks", json::array())) {
    if (!check.is_object()) continue;
    // 只认「被采为计分单位」的 supported：重复陈述的同一字段会去重，
    // 拿它当加分项会让学员靠重复刷分。
    if (check.value("scoringUnit", false) && evidenceString(check, "verdict") == "supported") ++accurate;
    if (check.value("correctedInLaterRound", false)) ++corrected;
  }
  // 正确转诊：承认需要医生判断而不是自己给结论。deferred_to_doctor 是核验器
  // 内部的集合，不在输出里，因此按「无违规且未被判incomplete 的治疗类字段」近似。
  for (const auto& check : assessment.value("knowledgeChecks", json::array())) {
    if (!check.is_object() || evidenceString(check, "verdict") == "not_applicable") continue;
    const auto topic = evidenceString(check, "topic");
    if (topic == "treatmentDuration" || topic == "visitDuration" || topic == "followupInterval")
      if (evidenceString(check, "reason").find("医生") != std::string::npos) ++deferred;
  }
  const auto objections = patient_state.value("triggeredObjections", json::array());
  const int penalty = (patient_state.value("riskTriggered", false) ? 10 : 0)
                    + static_cast<int>(objections.size()) * 5;
  const int gain = accurate * 2 + corrected * 3 + deferred * 2;
  const int net = gain - penalty;
  // 档位只描述「这场建立了多少信任」，不换算成百分制—— 避免与五维分数混淆，
  // 也避免学员把它当成可以刷的考核项。
  const char* band = net >= 12 ? "strong" : net >= 5 ? "fair" : net >= 0 ? "neutral" : "damaged";
  return {{"band", band}, {"net", net}, {"accurateStatements", accurate},
          {"selfCorrections", corrected}, {"properDeferrals", deferred},
          {"triggeredObjections", objections.size()}, {"penalty", penalty},
          {"gain", gain}, {"rubricVersion", "trust-building-v1"}};
}

inline json applyKnowledgeScores(json report,const json& assessment,int pass_score=60,
                                 const json& patient_state=json::object()) {
  const auto score=assessment.at("knowledgeAssessment").at("knowledgeAccuracy");
  report["schemaVersion"]=2;
  for(const auto* key:{"knowledgeAssessment","knowledgeChecks","knowledgeManifestHash","deferredTopics"}) report[key]=assessment.at(key);
  report["dimensionScores"]["knowledgeAccuracy"]=score;
  // 信任度正向评价：只在此处结算，不影响 dimensionScores 与 totalScore。
  report["trustSummary"]=summarizeTrustBuilding(assessment,patient_state);
  report["totalScore"]=nullptr; report["passed"]=nullptr;
  const auto& d=report.at("dimensionScores");
  for(const auto* key:{"medicalCompliance","empathy","needsDiscovery","serviceEtiquette"})
    if(!d.contains(key)||!d[key].is_number_integer()||d[key]<0||d[key]>100) throw std::runtime_error("invalid communication score");
  if(!score.is_null()) {
    const int total=static_cast<int>(std::lround(score.get<int>()*.25+d["medicalCompliance"].get<int>()*.25+
        d["empathy"].get<int>()*.20+d["needsDiscovery"].get<int>()*.20+d["serviceEtiquette"].get<int>()*.10));
    report["totalScore"]=total; report["passed"]=total>=pass_score;
  }
  return report;
}
} // namespace oral_training::rag
