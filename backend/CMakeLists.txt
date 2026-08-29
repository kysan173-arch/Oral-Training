cmake_minimum_required(VERSION 3.20)
project(oral_training_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(oral_training_backend src/main.cpp)
add_executable(report_validation_test tests/report_validation_test.cpp)
add_executable(database_feature_test tests/database_feature_test.cpp)

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
target_link_libraries(database_feature_test PRIVATE ${ORAL_TRAINING_LIBRARIES})

target_compile_definitions(oral_training_backend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(report_validation_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(database_feature_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

enable_testing()
add_test(NAME report_validation COMMAND report_validation_test)
add_test(NAME database_feature COMMAND database_feature_test)
set_tests_properties(database_feature PROPERTIES SKIP_RETURN_CODE 77)
add_test(
  NAME client_static_checks
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_checks.ps1
)

if(MSVC)
  target_compile_options(oral_training_backend PRIVATE /utf-8)
  target_compile_options(report_validation_test PRIVATE /utf-8)
  target_compile_options(database_feature_test PRIVATE /utf-8)
  target_compile_definitions(oral_training_backend PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(report_validation_test PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(database_feature_test PRIVATE _WIN32_WINNT=0x0A00)
endif()
