cmake_minimum_required(VERSION 3.20)
project(oral_training_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # ⚠️ /EHsc 必须显式声明。CMake 对 MSVC 的默认 CMAKE_CXX_FLAGS 本含 /EHsc，
  # 但本机缓存里它曾只剩 /bigobj（CMAKE_CXX_FLAGS 被外部 CXXFLAGS 环境变量覆盖），
  # 导致 <ExceptionHandling> 为空：**抛异常时不做栈展开，局部对象的析构函数不会执行**。
  # 后果是任何 4xx/5xx 都会泄漏 RAII 资源——pqxx::work 不回滚（事务和行锁留在
  # 服务器上）、连接守卫不归还池。实测复现：一次 400 的 PUT 之后，对同一场景的
  # 后续写请求永久等锁（pg_stat_activity 可见 idle in transaction 持锁 + Lock 等待）。
  # 写在这里而不是依赖缓存，命令行编译也要带上（cl /EHsc）。
  add_compile_options(/EHsc)
endif()

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Avoid network updates after initial dependency fetch" FORCE)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CROW_FEATURES "" CACHE STRING "" FORCE)
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
  FetchContent_Populate(asio)
endif()
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "Standalone Asio headers" FORCE)
FetchContent_Declare(
  crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.1
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  libpqxx
  GIT_REPOSITORY https://github.com/jtv/libpqxx.git
  GIT_TAG 7.10.1
  GIT_SHALLOW TRUE
)

# The Windows development setup installs libpq with PostgreSQL 18.
set(PostgreSQL_ROOT "C:/Program Files/PostgreSQL/18" CACHE PATH "PostgreSQL installation root")
find_package(PostgreSQL REQUIRED)

FetchContent_MakeAvailable(crow nlohmann_json libpqxx)

add_executable(oral_training_backend
  src/main.cpp
  src/knowledge_store.cpp
  src/knowledge_admin_queue.cpp
  src/rag_retriever.cpp
)
add_executable(report_validation_test tests/report_validation_test.cpp)
add_executable(security_config_test tests/security_config_test.cpp)
add_executable(variant_test tests/variant_test.cpp)
add_executable(database_feature_test tests/database_feature_test.cpp)
add_executable(rag_contract_test tests/rag_contract_test.cpp)
add_executable(rag_retriever_test tests/rag_retriever_test.cpp src/rag_retriever.cpp)
add_executable(knowledge_store_test tests/knowledge_store_test.cpp src/knowledge_store.cpp src/rag_retriever.cpp)
add_executable(knowledge_store_database_test
  tests/knowledge_store_database_test.cpp
  src/knowledge_store.cpp
  src/knowledge_admin_queue.cpp
  src/rag_retriever.cpp
)
# AI 骨架生成（P1-3）的端到端验证：注入假模型网关，其余全真（真库、真迁移、真队列、
# 真 worker、真校验）。它需要 main.cpp 里的 Service / ReliableDatabase 与各辅助函数，
# 所以与 database_feature_test 一样直接 include main.cpp（ORAL_TRAINING_NO_MAIN）；
# 又因为用的是真的 Service（含知识队列与 RAG 检索器），必须一起编那三个源文件。
add_executable(scenario_draft_test
  tests/scenario_draft_test.cpp
  src/knowledge_store.cpp
  src/knowledge_admin_queue.cpp
  src/rag_retriever.cpp
)

set(ORAL_TRAINING_LIBRARIES
  Crow::Crow
  pqxx
  nlohmann_json::nlohmann_json
  PostgreSQL::PostgreSQL
  ws2_32
  winhttp
  bcrypt
)

target_link_libraries(oral_training_backend PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(report_validation_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(security_config_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(variant_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(database_feature_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(rag_contract_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(rag_retriever_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(knowledge_store_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(knowledge_store_database_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(scenario_draft_test PRIVATE ${ORAL_TRAINING_LIBRARIES})

target_compile_definitions(oral_training_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(report_validation_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(security_config_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(variant_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(database_feature_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(rag_contract_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(knowledge_store_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(knowledge_store_database_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(scenario_draft_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

enable_testing()
add_test(NAME report_validation COMMAND report_validation_test)
add_test(NAME security_config COMMAND security_config_test)
add_test(NAME variant COMMAND variant_test)
add_test(NAME client_recovery COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_recovery_test.js)
add_test(NAME database_feature COMMAND database_feature_test)
set_tests_properties(database_feature PROPERTIES SKIP_RETURN_CODE 77)
# 需要 ORAL_TRAINING_TEST_DATABASE_URL；未配置时返回 77（Skipped）
add_test(NAME scenario_draft COMMAND scenario_draft_test)
set_tests_properties(scenario_draft PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME rag_contract COMMAND rag_contract_test)
add_test(NAME rag_retriever COMMAND rag_retriever_test)
add_test(NAME knowledge_store COMMAND knowledge_store_test)
add_test(
  NAME client_static_checks
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_checks.ps1
)

if(MSVC)
  target_compile_options(oral_training_backend PRIVATE /utf-8)
  target_compile_options(report_validation_test PRIVATE /utf-8)
  target_compile_options(security_config_test PRIVATE /utf-8)
  target_compile_options(variant_test PRIVATE /utf-8)
  target_compile_options(database_feature_test PRIVATE /utf-8)
  target_compile_options(rag_contract_test PRIVATE /utf-8)
  target_compile_options(rag_retriever_test PRIVATE /utf-8)
  target_compile_options(knowledge_store_test PRIVATE /utf-8)
  target_compile_options(knowledge_store_database_test PRIVATE /utf-8)
  target_compile_options(scenario_draft_test PRIVATE /utf-8)
  target_compile_definitions(oral_training_backend PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(report_validation_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(security_config_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(database_feature_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(rag_contract_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(rag_retriever_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(knowledge_store_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(knowledge_store_database_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(scenario_draft_test PRIVATE _WIN32_WINNT=0x0A00)
endif()
