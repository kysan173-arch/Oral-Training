#define ORAL_TRAINING_NO_MAIN
#include "../src/main.cpp"

#include <iostream>

namespace {

void setEnvironment(const char* name, const char* value) {
  if (_putenv_s(name, value) != 0) throw std::runtime_error("failed to set test environment");
}

void setValidProductionEnvironment() {
  setEnvironment("DATABASE_URL", "postgresql://test@127.0.0.1:5432/test");
  setEnvironment("DEEPSEEK_API_KEY", "");
  setEnvironment("DEEPSEEK_MODEL", "deepseek-v4-flash");
  setEnvironment("PRODUCTION", "true");
  setEnvironment("AUTH_MODE", "wechat");
  setEnvironment("AUTH_TOKEN_TTL_SECONDS", "604800");
  setEnvironment("WECHAT_APP_ID", "test-app-id");
  setEnvironment("WECHAT_APP_SECRET", "test-app-secret");
  setEnvironment("ALLOW_RUNTIME_API_KEY", "false");
  setEnvironment("BIND_ADDRESS", "127.0.0.1");
  setEnvironment("PORT", "8080");
  setEnvironment("ALLOWED_ORIGIN", "https://mini.example.com");
  setEnvironment("REQUIRE_HTTPS", "true");
  setEnvironment("TRUSTED_PROXY_IPS", "127.0.0.1, ::1");
  setEnvironment("RATE_LIMIT_PER_MINUTE", "120");
  setEnvironment("AI_WORKER_CONCURRENCY", "1");
  setEnvironment("DATABASE_POOL_SIZE", "12");
  setEnvironment("DATABASE_POOL_WAIT_MS", "3000");
}

template <typename Function>
bool throwsRuntimeError(Function&& function) {
  try {
    function();
    return false;
  } catch (const std::runtime_error&) {
    return true;
  }
}

}  // namespace

int main() {
  setValidProductionEnvironment();
  const auto production = Config::fromEnvironment();
  if (!production.production || production.auth_mode != "wechat" ||
      !production.require_https || production.trusted_proxy_ips.size() != 2 ||
      production.database_pool_size != 12 || production.database_pool_wait_ms != 3000) {
    std::cerr << "valid production configuration was not accepted\n";
    return 1;
  }

  setValidProductionEnvironment();
  setEnvironment("AI_WORKER_CONCURRENCY", "4");
  setEnvironment("DATABASE_POOL_SIZE", "5");
  if (!throwsRuntimeError([] { (void)Config::fromEnvironment(); })) {
    std::cerr << "undersized database pool was accepted\n";
    return 1;
  }

  setEnvironment("PRODUCTION", "tru");
  if (!throwsRuntimeError([] { (void)Config::fromEnvironment(); })) {
    std::cerr << "invalid boolean configuration was accepted\n";
    return 1;
  }

  setValidProductionEnvironment();
  setEnvironment("AUTH_MODE", "demo");
  if (!throwsRuntimeError([] { (void)Config::fromEnvironment(); })) {
    std::cerr << "demo authentication was accepted in production\n";
    return 1;
  }

  setValidProductionEnvironment();
  setEnvironment("REQUIRE_HTTPS", "false");
  if (!throwsRuntimeError([] { (void)Config::fromEnvironment(); })) {
    std::cerr << "disabled HTTPS enforcement was accepted in production\n";
    return 1;
  }

  setValidProductionEnvironment();
  setEnvironment("TRUSTED_PROXY_IPS", "");
  if (!throwsRuntimeError([] { (void)Config::fromEnvironment(); })) {
    std::cerr << "missing trusted proxy configuration was accepted in production\n";
    return 1;
  }

  const std::vector<std::string> trusted = {"127.0.0.1", "10.0.0.2"};
  if (resolveClientAddress("203.0.113.7", "198.51.100.9", trusted) != "203.0.113.7") {
    std::cerr << "untrusted peer was allowed to spoof X-Forwarded-For\n";
    return 1;
  }
  if (resolveClientAddress("127.0.0.1", "198.51.100.9, 10.0.0.2", trusted) !=
      "198.51.100.9") {
    std::cerr << "trusted proxy chain did not resolve the client address\n";
    return 1;
  }
  if (resolveClientAddress("::ffff:127.0.0.1", "198.51.100.8", trusted) !=
      "198.51.100.8") {
    std::cerr << "IPv4-mapped trusted proxy was not normalized\n";
    return 1;
  }
  if (requestForwardedAsHttps("203.0.113.7", "https", trusted) ||
      !requestForwardedAsHttps("127.0.0.1", "http, https", trusted) ||
      requestForwardedAsHttps("127.0.0.1", "https, http", trusted)) {
    std::cerr << "forwarded HTTPS trust boundary is incorrect\n";
    return 1;
  }

  try {
    (void)resolveClientAddress("127.0.0.1", "deadbeef", trusted);
    std::cerr << "malformed X-Forwarded-For was accepted\n";
    return 1;
  } catch (const ApiError& error) {
    if (error.code != "FORWARDED_HEADER_INVALID") throw;
  }

  if (!workerStateHealthy(false, 2, 2, 0) ||
      workerStateHealthy(false, 2, 1, 0) ||
      workerStateHealthy(false, 2, 2, 1) ||
      workerStateHealthy(true, 2, 2, 0)) {
    std::cerr << "worker health state is incorrect\n";
    return 1;
  }
  if (!serviceReady(true, true, true, true) ||
      serviceReady(false, true, true, true) ||
      serviceReady(true, false, true, true) ||
      serviceReady(true, true, false, true) ||
      serviceReady(true, true, true, false) ||
      healthStatusCode(true) != 200 || healthStatusCode(false) != 503) {
    std::cerr << "service readiness state is incorrect\n";
    return 1;
  }

  std::cout << "security configuration tests passed\n";
  return 0;
}
