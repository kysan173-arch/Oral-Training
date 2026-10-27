#include "embedding_fixture.h"
#include "../src/model_completion.h"
#include "../src/service_reply_prompt.h"
#include "../src/evidence_validator.h"
#include <fstream>
int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--live") {
    std::cerr << "Opt-in only: --live <synthetic knowledge JSON>; run offline checks first.\n"; return 2;
  }
  try {
    const auto* url = std::getenv("ORAL_TRAINING_TEST_DATABASE_URL");
    const auto* key = std::getenv("MINIMAX_API_KEY");
    requireEmbedding(url && key && *key, "Missing isolated database or MiniMax key");
    EmbeddingFixture f(url);
    json data; std::ifstream(argv[2]) >> data;
    for (const auto& item : data.at("knowledgeItems"))
      f.add(item.at("title"), item.at("body"), item.at("metadata"), item.at("topic"));
    RagRetriever retriever(f.pool);
    const json scenario = {{"public", {{"title","合成种植牙客服训练"},
        {"description","全部知识均为项目自带的虚构诊所测试资料"}}}};
    const std::vector<std::string> questions = {"我那颗后槽牙空了好久了，想弄个固定的，能直接做吗？",
      "基础档种植体、标准基台、国产全瓷冠加手术和一次CBCT，示例总价是多少？",
      "谢谢，我先考虑一下。"};
    json history = json::array();
    const std::string model = std::getenv("MINIMAX_MODEL") ? std::getenv("MINIMAX_MODEL") : "MiniMax-M3.1-Flash";
    int calls = 0;
    for (size_t i=0; i<questions.size(); ++i) {
      const auto started = std::chrono::steady_clock::now();
      const json evidence = f.retrieve(retriever, questions[i]);
      if (i < 2) requireEmbedding(!evidence["passages"].empty(), "real semantic retrieval returned no evidence");
      history.push_back({{"role","learner_patient"},{"content",questions[i]}});
      const auto messages = oral_training::models::serviceReplyMessages(scenario,history,evidence);
      const auto request = oral_training::models::buildCompletionRequest(model,messages,
          oral_training::models::initialCompletionBudget(model,900),0.15,true);
      const auto response = postJson("https://api.minimax.cn/v1","/chat/completions",request,60,key);
      ++calls;
      requireEmbedding(response.status == 200,"MiniMax HTTP status " + std::to_string(response.status));
      const auto payload = json::parse(response.body);
      const auto& choice = payload.at("choices").at(0);
      requireEmbedding(choice.value("finish_reason","") == "stop","MiniMax completion did not finish");
      json output;
      try { output = oral_training::models::parseModelJsonContent(choice.at("message").at("content")); }
      catch (const ApiError&) {
        std::cout << json{{"diagnostic","invalid_json"},{"model",payload.value("model",model)},
            {"usage",payload.value("usage",json::object())},
            {"content",choice.at("message").at("content")}}.dump() << std::endl;
        throw;
      }
      const auto grounded = groundedReply(output,f.context(),evidence,"embedding-live-"+std::to_string(i),questions[i]);
      requireEmbedding(!grounded.at("reply").get<std::string>().empty(),"empty grounded answer");
      if (i < 2) requireEmbedding(!grounded.at("citations").empty(),"answer failed citation validation");
      if (i == 1) requireEmbedding(grounded.at("reply").get<std::string>().find("8900") != std::string::npos,
                                   "synthetic price answer lost expected total");
      if (i == 2) requireEmbedding(grounded.at("citations").empty(),"conversation invented citation");
      history.push_back({{"role","standard_customer"},{"content",grounded.at("reply")}});
      json titles = json::array();
      for (const auto& passage : evidence["passages"]) titles.push_back(passage["title"]);
      std::cout << json{{"retrievedTitles",titles},{"question",questions[i]}, {"retriever",evidence["retrieverVersion"]},
        {"embeddingModel",evidence["embeddingModel"]},{"passages",evidence["passages"].size()},
        {"model",payload.value("model",model)}, {"usage",payload.value("usage",json::object())},
        {"result",grounded},{"httpCalls",calls}, {"elapsedMs",std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now()-started).count()}}.dump() << std::endl;
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << "Live RAG probe failed: " << e.what() << '\n'; return 1; }
}
