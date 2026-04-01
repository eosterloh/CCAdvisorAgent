##Dependencies
##I will need: 
## Networking package for web scraping
## Json package for managing responses from Gemini
## and ABSL

#version and langauge management
cmake_minimum_required(VERSION 3.14)

project(AdvisorAgent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CPR_CURL_USE_LIBPSL OFF CACHE BOOL "Disable libpsl to avoid meson requirement" FORCE)

##include this so we can snag stuff from gh
include(FetchContent)

## eg
##FetchContent_Declare(
## name
## Github link
## pinned version
##)
## Make it availible
## FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 20260107.1
)

# 2. nlohmann/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
)

### CHANGE TO gRPC during block 8.
FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG        1.14.2
)

FetchContent_MakeAvailable(json cpr absl)

add_executable(
  AdvisorAgBuild
  main.cc
  tests/gemini_client_tests.cc
  tests/tool_calling_tests.cc
  tests/weaviate_client_tests.cc
  src/app/toolcalling/scraper_tool.cc
  src/app/geminiclient/gemini_generation.cc
  src/app/geminiclient/gemini_requests.cc
  src/app/geminiclient/gemini_embedding.cc
  src/app/weaviate/weaviate_port.cc
)

target_include_directories(AdvisorAgBuild PRIVATE ${CMAKE_SOURCE_DIR}/include)


target_link_libraries(AdvisorAgBuild PRIVATE 
  absl::strings
  absl::status
  absl::statusor
  absl::flat_hash_map
  absl::time

  cpr::cpr
  nlohmann_json::nlohmann_json
)

add_executable(
  scraper_script
  scripts/scraperscript.cc
  src/app/toolcalling/scraper_tool.cc
)

target_include_directories(scraper_script PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(scraper_script PRIVATE
  absl::strings
  absl::status
  absl::statusor
  cpr::cpr
)

add_executable(
  weaviate_tests_runner
  tests/weaviate_test_main.cc
  tests/weaviate_client_tests.cc
  src/app/weaviate/weaviate_port.cc
  src/app/geminiclient/gemini_embedding.cc
  src/app/geminiclient/gemini_requests.cc
)

target_include_directories(weaviate_tests_runner PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(weaviate_tests_runner PRIVATE
  absl::strings
  absl::status
  absl::statusor
  cpr::cpr
  nlohmann_json::nlohmann_json
)

add_executable(
  send_data_to_weaviate
  scripts/send_data_to_weaviate.cc
  src/app/weaviate/weaviate_port.cc
  src/app/geminiclient/gemini_embedding.cc
  src/app/geminiclient/gemini_requests.cc
)

target_include_directories(send_data_to_weaviate PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(send_data_to_weaviate PRIVATE
  absl::strings
  absl::status
  absl::statusor
  cpr::cpr
  nlohmann_json::nlohmann_json
)
