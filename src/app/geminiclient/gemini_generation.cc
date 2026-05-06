#include "../../../include/app/geminiclient/gemini_generation.hpp"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include <cpr/cpr.h>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace {
constexpr int kHttpTimeoutMs = 60000;
constexpr int kHttpConnectTimeoutMs = 10000;
}
GeminiGenerator::GeminiGenerator() { GeminiRequests{}; }
GeminiGenerator::~GeminiGenerator() {}
absl::Status GeminiGenerator::geminiGen(std::string_view query,
                                        std::string_view model) {
  cpr::Url endpoint;
  if (model == "lightweight") {
    endpoint = "https://generativelanguage.googleapis.com/v1beta/models/"
               "gemini-2.5-flash:generateContent";
  } else {
    endpoint = "https://generativelanguage.googleapis.com/v1beta/models/"
               "gemini-3.1-pro-preview:generateContent";
  }

  cpr::Header header{{"Content-Type", "application/json"}};
  cpr::Parameters params{{"key", apikey}};
  json bodyjson = {
      {"contents", {{{"parts", {{{"text", std::string(query)}}}}}}}};
  cpr::Body body = bodyjson.dump();
  cpr::Response r = cpr::Post(
      endpoint, params, header, body,
      cpr::Timeout{std::chrono::milliseconds{kHttpTimeoutMs}},
      cpr::ConnectTimeout{std::chrono::milliseconds{kHttpConnectTimeoutMs}});

  if (r.error) {
    LOG(ERROR) << "Gemini generation request transport failed: " << r.error.message;
    return absl::InternalError("Api request failed");
  }
  if (r.status_code >= 400) {
    LOG(ERROR) << "Gemini generation request failed with HTTP " << r.status_code
               << ", response: " << r.text;
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