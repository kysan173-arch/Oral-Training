#pragma once
#ifndef _WIN32

#include "secure_random.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace oral_training::secrets {
// This file is provisioned separately from the database. Never auto-generate a
// replacement: losing it must fail closed, not silently strand saved credentials.
struct MasterKey {
  std::array<unsigned char, 32> bytes{};
  MasterKey() {
    const char* path = std::getenv("MODEL_KEY_FILE");
    if (!path || path[0] != '/') throw std::runtime_error("MODEL_KEY_FILE must be an absolute path");
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) throw std::runtime_error("Cannot open MODEL_KEY_FILE");
    struct Close { int fd; ~Close() { close(fd); } } close_file{fd};
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 077) != 0 || info.st_size != 32)
      throw std::runtime_error("MODEL_KEY_FILE must be a private, owner-held 32-byte file (mode 0600)");
    if (read(fd, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
      OPENSSL_cleanse(bytes.data(), bytes.size());
      throw std::runtime_error("Cannot read MODEL_KEY_FILE");
    }
  }
  ~MasterKey() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
  MasterKey(const MasterKey&) = delete;
  MasterKey& operator=(const MasterKey&) = delete;
};

inline std::string hex(const std::vector<unsigned char>& bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  for (auto c : bytes) { out += digits[c >> 4]; out += digits[c & 15]; }
  return out;
}
inline std::vector<unsigned char> unhex(const std::string& value) {
  if (value.size() % 2) throw std::runtime_error("Invalid ciphertext");
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::runtime_error("Invalid ciphertext");
  };
  std::vector<unsigned char> out;
  for (size_t i = 0; i < value.size(); i += 2)
    out.push_back(static_cast<unsigned char>((digit(value[i]) << 4) | digit(value[i + 1])));
  return out;
}
inline constexpr char kEnvelope[] = "aes256gcm:v1:";
inline void requireCrypto(int result) {
  if (result != 1) throw std::runtime_error("Credential encryption/authentication failed");
}
using Cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

inline std::string encrypt(const std::string& secret) {
  if (secret.empty() || secret.size() > 2048) throw std::runtime_error("Invalid credential size");
  MasterKey key;
  std::vector<unsigned char> out(12 + 16 + secret.size());
  secureRandom(out.data(), 12);
  Cipher ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx) throw std::runtime_error("Cannot allocate cipher");
  requireCrypto(EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.bytes.data(), out.data()));
  int length = 0, tail = 0;
  requireCrypto(EVP_EncryptUpdate(ctx.get(), nullptr, &length,
      reinterpret_cast<const unsigned char*>(kEnvelope), sizeof(kEnvelope) - 1));
  requireCrypto(EVP_EncryptUpdate(ctx.get(), out.data() + 28, &length,
      reinterpret_cast<const unsigned char*>(secret.data()), static_cast<int>(secret.size())));
  requireCrypto(EVP_EncryptFinal_ex(ctx.get(), out.data() + 28 + length, &tail));
  requireCrypto(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, out.data() + 12));
  return std::string(kEnvelope) + hex(out);
}

inline std::string decrypt(const std::string& encrypted) {
  const std::string prefix = kEnvelope;
  if (encrypted.rfind(prefix, 0) != 0 || encrypted.size() > prefix.size() + (2048 + 28) * 2)
    throw std::runtime_error("Unsupported credential format; re-enter the personal API key");
  const auto input = unhex(encrypted.substr(prefix.size()));
  if (input.size() <= 28) throw std::runtime_error("Invalid ciphertext");
  MasterKey key;
  Cipher ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx) throw std::runtime_error("Cannot allocate cipher");
  requireCrypto(EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.bytes.data(), input.data()));
  std::vector<unsigned char> output(input.size());
  struct Wipe { std::vector<unsigned char>& value; ~Wipe() { OPENSSL_cleanse(value.data(), value.size()); } } wipe{output};
  int length = 0, tail = 0;
  requireCrypto(EVP_DecryptUpdate(ctx.get(), nullptr, &length,
      reinterpret_cast<const unsigned char*>(kEnvelope), sizeof(kEnvelope) - 1));
  requireCrypto(EVP_DecryptUpdate(ctx.get(), output.data(), &length, input.data() + 28,
      static_cast<int>(input.size() - 28)));
  requireCrypto(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16,
      const_cast<unsigned char*>(input.data()) + 12));
  requireCrypto(EVP_DecryptFinal_ex(ctx.get(), output.data() + length, &tail));
  return std::string(reinterpret_cast<char*>(output.data()), length + tail);
}
}  // namespace oral_training::secrets
#endif
