#include "../src/rag_retriever.h"
#include "../src/rag_manifest.h"
#include "../src/evidence_conflicts.h"
#include "../src/knowledge_validity.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool has(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

int main() {
  try {
    using namespace oral_training::rag;
    require(contradictoryClaims("本服务包含拍片。", "本服务不包含拍片。"), "opposite inclusion must conflict");
    require(contradictoryClaims("需复诊2次。", "需复诊3次。"), "inconsistent quantities must conflict");
    require(contradictoryClaims("需要复诊。", "无需复诊。"), "opposite requirement must conflict");
    require(contradictoryClaims("可以拍片。", "不可以拍片。"), "opposite permission must conflict");
    require(!contradictoryClaims("包含3次拍片。", "不包含2次拍片。"), "different numeric referents are not opposite claims");
    require(!contradictoryClaims("本服务包含拍片。", "本服务包含咨询。"), "complementary facts must coexist");
    require(!contradictoryClaims("本服务不包含拍片。", "本服务不包含拍片。"), "duplicates are not conflicts");
    require(!contradictoryClaims("不保证一次完成。", "需要医生评估。"), "unrelated qualifiers must coexist");
    const nlohmann::json validity = {{"validFrom", "2026-09-01"}, {"validUntil", "2026-09-30"}};
    require(effectiveOnDate(validity, "validFrom", "validUntil", "2026-09-30"), "last valid day is inclusive");
    require(!effectiveOnDate(validity, "validFrom", "validUntil", "2026-10-01"), "expired price excluded");
    require(!effectiveOnDate(validity, "validFrom", "validUntil", "2026-08-31"), "future price excluded");
    oral_training::rag::RagRetriever retriever(nullptr);
    oral_training::rag::RetrievalRequest request;
    request.purpose = RetrievalPurpose::CustomerReply;
    request.current_question = "太长了，一句话说清楚就行。";
    request.recent_question_answers = nlohmann::json::array({
      {{"role", "learner_patient"}, {"content", "大概需要多久才能完成？"}},
      {{"role", "standard_customer"}, {"content", "untrusted assistant facts"}},
      {{"role", "learner_patient"}, {"content", request.current_question}}});
    require(customerRetrievalQuestion(request).find("多久") != std::string::npos, "shortening request retains topic");
    require(customerRetrievalQuestion(request).find("untrusted") == std::string::npos, "assistant text not evidence");
    request.current_question = "我预算不多，太贵了。";
    require(customerRetrievalQuestion(request) == request.current_question, "new topic does not inherit old question");
    require(has(tokenizeChinese(request.current_question), "a_price"), "colloquial price question retrieves prices");
    require(has(tokenizeChinese("能当天弄完吗"), "a_duration"), "colloquial duration question retrieves duration");
    bool mismatched_hash_rejected = false;
    try {
      retriever.retrieve("sr1", {"kr1"}, "2026-09-20", "kr1", request);
    } catch (const std::runtime_error&) { mismatched_hash_rejected = true; }
    require(mismatched_hash_rejected, "manifest mismatch must fail before database access");
    const auto price = oral_training::rag::tokenizeChinese("这个项目多少钱？");
    const auto quote = oral_training::rag::tokenizeChinese("请问报价和收费标准");
    require(has(price, "a_price") && has(quote, "a_price"), "price aliases must converge");

    const auto full_width = oral_training::rag::tokenizeChinese("ＡＢＣ １２３");
    require(has(full_width, "w_abc") && has(full_width, "w_123"),
            "full-width ASCII must normalize");

    const std::string sentence = "种植牙不保证一次完成，也不代表无需复诊。";
    const nlohmann::json metadata = {
        {"aliases", {"种牙"}}, {"applicability", "成年人模拟咨询"}};
    const auto short_chunks = oral_training::rag::chunkKnowledge("种植说明", sentence, metadata);
    require(short_chunks.size() == 1 && short_chunks[0].body == sentence,
            "chunking must preserve negation and original text");
    require(has(oral_training::rag::tokenizeChinese(short_chunks[0].body), "a_dental_implant"),
            "controlled implant alias must be indexed");

    std::string long_body;
    for (int i = 0; i < 1100; ++i) long_body += "牙";
    const auto chunks = oral_training::rag::chunkKnowledge("长文", long_body, metadata);
    require(chunks.size() >= 3, "long text must be split");
    for (std::size_t i = 0; i < chunks.size(); ++i) {
      require(chunks[i].ordinal == static_cast<int>(i), "chunk ordinals must be stable");
      require(chunks[i].source_end > chunks[i].source_start, "chunk source offsets required");
      require(chunks[i].source_end - chunks[i].source_start <= 450,
              "chunk must stay below database limit");
    }

    require(oral_training::rag::tokenizeChinese("牙套费用") ==
            oral_training::rag::tokenizeChinese("牙套费用"), "tokenization must be deterministic");
    std::cout << "rag retriever tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
