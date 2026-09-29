#pragma once

#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace oral_training {
#ifdef _WIN32
inline std::string sha256Hex(const std::string& value) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD hash_size = 0;
  DWORD result_size = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size), &result_size, 0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_size),
                        sizeof(hash_size), &result_size, 0) != 0) {
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    throw std::runtime_error("SHA-256 initialization failed");
  }
  std::vector<unsigned char> object(object_size);
  std::vector<unsigned char> digest(hash_size);
  const auto create_status = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0);
  const auto update_status = create_status == 0
      ? BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())),
                       static_cast<ULONG>(value.size()), 0)
      : create_status;
  const auto finish_status = update_status == 0
      ? BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0)
      : update_status;
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (finish_status != 0) throw std::runtime_error("SHA-256 failed");
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest) output << std::setw(2) << static_cast<int>(byte);
  return output.str();
}

#else
inline std::string sha256Hex(const std::string& value) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (EVP_Digest(value.data(), value.size(), digest, &size, EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("SHA-256 failed");
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < size; ++i) output << std::setw(2) << static_cast<int>(digest[i]);
  return output.str();
}
#endif
}  // namespace oral_training
