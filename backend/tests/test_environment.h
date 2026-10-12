#pragma once
#include <cstdlib>
#include <stdexcept>
inline void setTestEnvironment(const char* name, const char* value) {
#ifdef _WIN32
  const int status = _putenv_s(name, value);
#else
  const int status = setenv(name, value, 1);
#endif
  if (status != 0) throw std::runtime_error("failed to set test environment");
}
#ifndef _WIN32
#include "../src/secure_random.h"
#include <unistd.h>
#include <string>
struct TestKeyFile {
  std::string path;
  std::string old;
  bool had_old = false;
  TestKeyFile() {
    if (const char* value = std::getenv("MODEL_KEY_FILE")) { old = value; had_old = true; }
    char name[] = "/tmp/oral-key-test-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) throw std::runtime_error("test key creation failed");
    path = name;
    unsigned char key[32];
    oral_training::secureRandom(key, sizeof(key));
    const auto size = write(fd, key, sizeof(key));
    close(fd);
    if (size != sizeof(key)) { unlink(path.c_str()); throw std::runtime_error("test key write failed"); }
    setTestEnvironment("MODEL_KEY_FILE", path.c_str());
  }
  ~TestKeyFile() {
    unlink(path.c_str());
    if (had_old) setenv("MODEL_KEY_FILE", old.c_str(), 1); else unsetenv("MODEL_KEY_FILE");
  }
};
#endif
