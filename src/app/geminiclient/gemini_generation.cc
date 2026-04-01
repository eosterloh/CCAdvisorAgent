#include "../../../include/app/geminiclient/gemini_generation.hpp"
#include "absl/status/status.h"
#include <cpr/cpr.h>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
GeminiGenerator::GeminiGenerator() { GeminiRequests{}; }
GeminiGenerator::~GeminiGenerator() {}
absl::Status GeminiGenerator::geminiGen(std::string_view query) {

  cpr::Url endpoint{"https://generativelanguage.googleapis.com/v1beta/models/"
                    "gemini-2.5-flash:generateContent"};
  cpr::Header header{{"Content-Type", "application/json"}};
  cpr::Parameters params{{"key", apikey}};
  json bodyjson = {
      {"contents", {{{"parts", {{{"text", std::string(query)}}}}}}}};
  cpr::Body body = bodyjson.dump();
  cpr::Response r = cpr::Post(endpoint, params, header, body);

  if (r.error) {
    return absl::InternalError("Api request failed");
  } else {
    msg_content = r.text;
    return absl::OkStatus();
  }
}
absl::Status GeminiGenerator::bindTool(std::string_view tool) {
  // do nothing for now
  return absl::OkStatus();
}