#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"
#include <future>

void checkSetting(bool condition) {
  if (!condition) throw std::runtime_error("LiteLLM settings assertion failed");
}

int main() {
  const auto local = gatewayEndpoint("http://127.0.0.1:4000/v1/");
  checkSetting(local.port == 4000 && !local.secure && local.path == L"/v1/chat/completions");
  const auto remote = gatewayEndpoint("https://llm.example.com/gateway/v1");
  checkSetting(remote.secure && remote.path == L"/gateway/v1/chat/completions");
  checkSetting(gatewayEndpoint("https://llm.example.com").path == L"/v1/chat/completions");
  for (const auto* invalid : {"http://example.com/v1", "https://user:pass@example.com/v1",
       "https://example.com/v1?key=secret", "https://example.com/v1#fragment",
       "https://example.com/v1/chat/completions", "https://example.com/\r\n", "file:///secret"}) {
    bool rejected = false;
    try { gatewayEndpoint(invalid); } catch (const ApiError&) { rejected = true; }
    checkSetting(rejected);
  }
  const std::string secret = "sk-offline-test-secret";
  const auto encrypted = protectGatewayKey(secret);
  checkSetting(encrypted.find(secret) == std::string::npos && unprotectGatewayKey(encrypted) == secret);
  bool rejected = false;
  try { unprotectGatewayKey("bad-ciphertext"); } catch (const ApiError&) { rejected = true; }
  checkSetting(rejected);
  GatewaySettings settings{"https://gateway.example/v1", "ds-primary", encrypted, 3};
  checkSetting(settings.status().dump().find(secret) == std::string::npos);
  checkSetting(!settings.status().contains("apiKey") && settings.status()["scope"] == "personal");
  _putenv_s("DEEPSEEK_API_KEY", secret.c_str());
  _putenv_s("DEEPSEEK_MODEL", "ignored-model");
  checkSetting(!ModelGateway(Config{}).configured());
  const auto request = buildCompletionRequest("ds-primary", json::array(), 1000, 0.2, true);
  checkSetting(request["model"] == "ds-primary" && !request.contains("thinking") && !request.contains("user_id"));
  // Optional integration mode runs only against the disposable harness database.
  const auto* database_url = std::getenv("LITELLM_TEST_DATABASE_URL");
  if (database_url && *database_url) {
    auto pool = std::make_shared<DatabasePool>(database_url, 4, std::chrono::milliseconds(1000));
    { auto c = pool->acquire(); pqxx::read_transaction tx(c.get());
      checkSetting(tx.exec("SELECT current_database()")[0][0].as<std::string>() == "litellm_config_test"); }
    GatewaySettingsStore store(pool);
    const std::string owner = "demo-user-001";
    const auto saved = store.load(owner);
    checkSetting(saved.configured() && saved.model == "ds-primary");
    ModelGateway gateway(Config{}, pool, owner);
    checkSetting(gateway.configured());
    const auto reply = gateway.groundedPatientReply(json::object(), json::array(), json::array());
    checkSetting(reply["intent"] == "clarify" && gateway.modelCallCount() == 1);
    checkSetting(gateway.modelVersion() == "litellm:ds-primary@" + std::to_string(saved.revision));
    gateway.patientReply({{"public", json::object()}, {"hidden", json::object()}}, json::object(), json::array());
    gateway.standardServiceReply({{"public", json::object()}, {"serviceGuidance", json::object()}}, json::array());
    auto updated = store.save({{"revision", saved.revision}, {"baseUrl", saved.base_url}, {"model", "json-repair"}}, owner);
    checkSetting(gateway.groundedPatientReply(json::object(), json::array(), json::array())["intent"] == "clarify");
    checkSetting(gateway.modelCallCount() == 5); // Three normal calls, one invalid response + repair.
    store.save({{"revision", updated.revision}, {"baseUrl", saved.base_url}, {"model", "ds-primary"}}, owner);
    // Bound gateways retain user identity across concurrent requests. A shared
    // process-wide HTTP budget still applies to all of the bound instances.
    auto first = gateway.forUser(owner);
    auto second = gateway.forUser("litellm-test-admin");
    auto a = std::async(std::launch::async, [&] {
      return first->groundedPatientReply(json::object(), json::array(), json::array());
    });
    auto b = std::async(std::launch::async, [&] {
      return second->groundedPatientReply(json::object(), json::array(), json::array());
    });
    checkSetting(a.get()["userMarker"] == "ds-primary");
    checkSetting(b.get()["userMarker"] == "peer-model");
    checkSetting(gateway.modelCallCount() == 7);
    checkSetting(!gateway.forUser("unconfigured-user")->configured());
    try {
      gateway.forUser("unconfigured-user")->groundedPatientReply(json::object(), json::array(), json::array());
      throw std::runtime_error("missing personal config was not rejected");
    } catch (const ApiError& error) { checkSetting(error.code == "MODEL_NOT_CONFIGURED"); }
  }
  std::cout << "LiteLLM configuration tests passed\n";
}
