#pragma once

// 全项目唯一的 API 错误类型。
//
// 为什么单独放一个头文件：`reliable_store.h` 里到处 throw ApiError（建场景校验等），
// 但 ApiError 原本只定义在 main.cpp 的匿名命名空间里——也就是说 reliable_store.h
// 只能被 main.cpp（或 include 了 main.cpp 的测试）编译，任何独立的 store 测试
// 都拿不到它。把它提出来，跨模块复用（如 knowledge 层要调用场景校验）才成立。
//
// 兼容位置保持不变：main.cpp 仍在本文件被包含后直接使用 ApiError，全局唯一一份定义。

#include <stdexcept>
#include <string>
#include <utility>

class ApiError : public std::runtime_error {
 public:
  ApiError(int http_status, std::string code, std::string message)
      : std::runtime_error(message), http_status(http_status), code(std::move(code)) {}

  int http_status;
  std::string code;
};
