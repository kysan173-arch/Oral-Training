#pragma once

#include <cstddef>
#include <climits>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/rand.h>
#endif

namespace oral_training {
inline void secureRandom(unsigned char* bytes, size_t size) {
  if (size > INT_MAX) throw std::runtime_error("random request too large");
#ifdef _WIN32
  const bool ok = BCryptGenRandom(nullptr, bytes, static_cast<ULONG>(size),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
  const bool ok = RAND_bytes(bytes, static_cast<int>(size)) == 1;
#endif
  if (!ok) throw std::runtime_error("secure random generation failed");
}
}  // namespace oral_training
