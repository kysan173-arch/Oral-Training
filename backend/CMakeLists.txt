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

add_library(embedding_client src/embedding_client.cpp)
target_link_libraries(embedding_client PUBLIC nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(embedding_client PUBLIC winhttp bcrypt)
  target_compile_definitions(embedding_client PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  if(MSVC)
    target_compile_options(embedding_client PRIVATE /utf-8)
  endif()
else()
  find_package(OpenSSL REQUIRED)
  set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
                      GIT_TAG v0.18.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(httplib)
  target_link_libraries(embedding_client PUBLIC httplib::httplib OpenSSL::Crypto)
endif()

add_executable(embedding_client_test tests/embedding_client_test.cpp)
target_link_libraries(embedding_client_test PRIVATE embedding_client)
add_executable(rag_conversation_probe tests/rag_conversation_probe.cpp src/rag_retriever.cpp)
target_link_libraries(rag_conversation_probe PRIVATE embedding_client pqxx PostgreSQL::PostgreSQL)
add_executable(embedding_rag_probe tests/embedding_rag_probe.cpp src/rag_retriever.cpp)
target_link_libraries(embedding_rag_probe PRIVATE embedding_client pqxx PostgreSQL::PostgreSQL)
add_executable(embedding_rag_test tests/embedding_rag_test.cpp src/rag_retriever.cpp)
target_link_libraries(embedding_rag_test PRIVATE embedding_client pqxx PostgreSQL::PostgreSQL)
# 这三个 probe/test 直接链 pqxx，而 pqxx 在 Windows 上要靠 Winsock（ioctlsocket /
# WSAGetLastError / WSAPoll）。主服务走 ORAL_TRAINING_LIBRARIES（第 162 行）已带
# ws2_32，它们不走那条路径 → MSVC 链接期报 LNK2019/LNK1120。
# Linux CI 不需要显式链接，故此前只在 Windows 暴露。
if(WIN32)
  foreach(target rag_conversation_probe embedding_rag_probe embedding_rag_test)
    target_link_libraries(${target} PRIVATE ws2_32)
  endforeach()
endif()
enable_testing()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME rag_conversation_evaluator
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/rag_conversation_evaluator_test.py)
endif()
add_test(NAME embedding_client COMMAND embedding_client_test)
add_test(NAME embedding_rag COMMAND embedding_rag_test)
set_tests_properties(embedding_rag PROPERTIES SKIP_RETURN_CODE 77)
if(MSVC)
  target_compile_options(embedding_client_test PRIVATE /utf-8)
  target_compile_options(rag_conversation_probe PRIVATE /utf-8)
  target_compile_options(embedding_rag_probe PRIVATE /utf-8)
  target_compile_options(embedding_rag_test PRIVATE /utf-8)
endif()

add_executable(oral_training_backend
  src/main.cpp
  src/knowledge_store.cpp
  src/knowledge_admin_queue.cpp
  src/rag_retriever.cpp
)
add_executable(report_validation_test tests/report_validation_test.cpp)
add_executable(security_config_test tests/security_config_test.cpp)
add_executable(variant_test tests/variant_test.cpp)
add_executable(litellm_settings_test tests/litellm_settings_test.cpp src/rag_retriever.cpp
  src/knowledge_store.cpp src/knowledge_admin_queue.cpp)
add_executable(service_reply_probe tests/service_reply_probe.cpp src/rag_retriever.cpp
  src/knowledge_store.cpp src/knowledge_admin_queue.cpp)
add_executable(database_feature_test tests/database_feature_test.cpp)
add_executable(patient_initialization_test tests/patient_initialization_test.cpp
  src/knowledge_store.cpp src/knowledge_admin_queue.cpp src/rag_retriever.cpp)
add_executable(audit_regression_test tests/audit_regression_test.cpp
  src/knowledge_store.cpp src/knowledge_admin_queue.cpp src/rag_retriever.cpp)
add_executable(rag_contract_test tests/rag_contract_test.cpp)
add_executable(evidence_validator_test tests/evidence_validator_test.cpp)
add_executable(patient_grounding_test tests/patient_grounding_test.cpp)
# 情绪累积演进的**行为**验证：与 patient_grounding_test 分工不同——
# 那边守"既有契约不许破"（34 项），这边守"演进确实发生"（逐轮爬升 /
# 无漂移 / 可逆 / 脏存量不自锁）。纯逻辑，不依赖 libpq 与数据库。
add_executable(patient_evolution_check tests/patient_evolution_check.cpp)
# v1 正向演进兜底的约束测试：纯逻辑、不依赖 libpq 与数据库。
# 独立成文件的原因：兜底逻辑原先内联在 main.cpp，无法单独链接测试，
# 而它恰恰最需要测试——改了没人发现，线上只表现为「情绪不动」。
add_executable(patient_positive_floor_test tests/patient_positive_floor_test.cpp)
add_executable(knowledge_evaluator_test tests/knowledge_evaluator_test.cpp)
add_executable(answer_comparison_test tests/answer_comparison_test.cpp)
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
  embedding_client
  pqxx
  nlohmann_json::nlohmann_json
  PostgreSQL::PostgreSQL
)
if(WIN32)
  list(APPEND ORAL_TRAINING_LIBRARIES ws2_32 winhttp bcrypt crypt32)
endif()

target_link_libraries(oral_training_backend PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(report_validation_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(security_config_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(variant_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(litellm_settings_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(service_reply_probe PRIVATE ${ORAL_TRAINING_LIBRARIES})
if(WIN32)
  target_compile_definitions(service_reply_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_compile_definitions(litellm_settings_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(database_feature_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(patient_initialization_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(audit_regression_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_compile_definitions(audit_regression_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(rag_contract_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(evidence_validator_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(patient_grounding_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(patient_evolution_check PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(patient_positive_floor_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(knowledge_evaluator_test PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(answer_comparison_test PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(evidence_validator_test PRIVATE bcrypt)
  target_link_libraries(patient_grounding_test PRIVATE bcrypt)
  target_link_libraries(patient_evolution_check PRIVATE bcrypt)
  target_link_libraries(patient_positive_floor_test PRIVATE bcrypt)
  target_link_libraries(knowledge_evaluator_test PRIVATE bcrypt)
  target_compile_definitions(evidence_validator_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(patient_grounding_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(patient_evolution_check PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(patient_positive_floor_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(knowledge_evaluator_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(evidence_validator_test PRIVATE OpenSSL::Crypto)
  target_link_libraries(patient_grounding_test PRIVATE OpenSSL::Crypto)
  target_link_libraries(patient_evolution_check PRIVATE OpenSSL::Crypto)
  target_link_libraries(patient_positive_floor_test PRIVATE OpenSSL::Crypto)
  target_link_libraries(knowledge_evaluator_test PRIVATE OpenSSL::Crypto)
endif()
target_link_libraries(rag_retriever_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(knowledge_store_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(knowledge_store_database_test PRIVATE ${ORAL_TRAINING_LIBRARIES})
target_link_libraries(scenario_draft_test PRIVATE ${ORAL_TRAINING_LIBRARIES})

target_compile_definitions(oral_training_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(report_validation_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(security_config_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(variant_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(database_feature_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(patient_initialization_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(rag_contract_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(rag_retriever_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(knowledge_store_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(knowledge_store_database_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(scenario_draft_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

enable_testing()
add_test(NAME report_validation COMMAND report_validation_test)
add_test(NAME security_config COMMAND security_config_test)
add_test(NAME variant COMMAND variant_test)
add_test(NAME litellm_settings COMMAND litellm_settings_test)
add_test(NAME model_settings_client COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/tests/model_settings_client_test.js)
add_test(NAME client_recovery COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_recovery_test.js)
add_test(NAME page_render COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/tests/page_render_check.js)
add_test(NAME database_feature COMMAND database_feature_test)
add_test(NAME patient_initialization COMMAND patient_initialization_test)
set_tests_properties(patient_initialization PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME audit_regression COMMAND audit_regression_test)
set_tests_properties(audit_regression PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(database_feature PROPERTIES SKIP_RETURN_CODE 77)
# 需要 ORAL_TRAINING_TEST_DATABASE_URL；未配置时返回 77（Skipped）
add_test(NAME scenario_draft COMMAND scenario_draft_test)
set_tests_properties(scenario_draft PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME rag_contract COMMAND rag_contract_test)
add_test(NAME evidence_validator COMMAND evidence_validator_test)
add_test(NAME patient_grounding COMMAND patient_grounding_test)
add_test(NAME patient_evolution COMMAND patient_evolution_check)
add_test(NAME patient_positive_floor COMMAND patient_positive_floor_test)
add_test(NAME knowledge_evaluator COMMAND knowledge_evaluator_test)
add_test(NAME answer_comparison COMMAND answer_comparison_test)
add_test(NAME roleplay_evidence COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/tests/roleplay_evidence_test.js)
add_test(NAME rag_retriever COMMAND rag_retriever_test)
add_test(NAME knowledge_store COMMAND knowledge_store_test)
if(WIN32)
  add_test(NAME client_static_checks COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_checks.ps1)
else()
  add_test(NAME client_static_checks COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_checks.py)
  add_executable(linux_key_store_test tests/linux_key_store_test.cpp)
  target_link_libraries(linux_key_store_test PRIVATE OpenSSL::Crypto)
  add_test(NAME linux_key_store COMMAND linux_key_store_test)
endif()

if(MSVC)
  target_compile_options(service_reply_probe PRIVATE /utf-8 /bigobj)
  target_compile_options(litellm_settings_test PRIVATE /utf-8 /bigobj)
  target_compile_options(oral_training_backend PRIVATE /utf-8 /bigobj)
  target_compile_options(report_validation_test PRIVATE /utf-8 /bigobj)
  target_compile_options(security_config_test PRIVATE /utf-8 /bigobj)
  target_compile_options(database_feature_test PRIVATE /utf-8 /bigobj)
  target_compile_options(patient_initialization_test PRIVATE /utf-8 /bigobj)
  target_compile_options(audit_regression_test PRIVATE /utf-8 /bigobj)
  target_compile_definitions(audit_regression_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_options(variant_test PRIVATE /utf-8 /bigobj)
  target_compile_options(rag_contract_test PRIVATE /utf-8)
  target_compile_options(evidence_validator_test PRIVATE /utf-8)
  target_compile_options(patient_grounding_test PRIVATE /utf-8)
  target_compile_options(patient_evolution_check PRIVATE /utf-8)
  target_compile_options(patient_positive_floor_test PRIVATE /utf-8)
  target_compile_options(knowledge_evaluator_test PRIVATE /utf-8)
  target_compile_options(answer_comparison_test PRIVATE /utf-8)
  target_compile_options(rag_retriever_test PRIVATE /utf-8)
  target_compile_options(knowledge_store_test PRIVATE /utf-8)
  target_compile_options(knowledge_store_database_test PRIVATE /utf-8)
  target_compile_options(scenario_draft_test PRIVATE /utf-8)
  target_compile_definitions(oral_training_backend PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(report_validation_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(security_config_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(database_feature_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(patient_initialization_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(rag_contract_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(rag_retriever_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(knowledge_store_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(knowledge_store_database_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(scenario_draft_test PRIVATE _WIN32_WINNT=0x0A00)
endif()
