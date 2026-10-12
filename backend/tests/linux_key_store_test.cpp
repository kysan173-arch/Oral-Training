#include "../src/linux_key_store.h"
#include "test_environment.h"
#include <iostream>
using namespace oral_training::secrets;
void check(bool ok) { if (!ok) throw std::runtime_error("key-store assertion failed"); }
template<class F> void rejects(F f) {
  bool rejected = false;
  try { f(); } catch (const std::exception&) { rejected = true; }
  check(rejected);
}
int main() {
  TestKeyFile file;
  const std::string key = "sk-private-test-key";
  const auto encrypted = encrypt(key);
  check(decrypt(encrypted) == key && encrypted.find(key) == std::string::npos);
  check(encrypt(key) != encrypted);
  for (size_t i : {size_t(12), size_t(36), encrypted.size() - 1}) {
    auto changed = encrypted;
    changed[i] = changed[i] == 'a' ? 'b' : 'a';
    rejects([&] { decrypt(changed); });
  }
  rejects([&] { decrypt(encrypted.substr(0, encrypted.size() - 2)); });
  rejects([&] { decrypt("01000000d08c9ddf"); }); // DPAPI is never mistaken for Linux ciphertext.
  {
    TestKeyFile other;
    rejects([&] { decrypt(encrypted); });
  }
  chmod(file.path.c_str(), 0644);
  rejects([&] { encrypt(key); });
  chmod(file.path.c_str(), 0600);
  const auto link = file.path + ".link";
  check(symlink(file.path.c_str(), link.c_str()) == 0);
  setTestEnvironment("MODEL_KEY_FILE", link.c_str());
  rejects([&] { encrypt(key); });
  unlink(link.c_str());
  setTestEnvironment("MODEL_KEY_FILE", "/tmp/nonexistent-oral-key-file");
  rejects([&] { decrypt(encrypted); });
  setTestEnvironment("MODEL_KEY_FILE", "relative-path");
  rejects([&] { encrypt(key); });
  setTestEnvironment("MODEL_KEY_FILE", file.path.c_str());
  check(truncate(file.path.c_str(), 31) == 0);
  rejects([&] { encrypt(key); });
  std::cout << "AES-GCM roundtrip, nonce, tamper, wrong key, permissions, symlink, missing/truncated key: PASS\n";
}
