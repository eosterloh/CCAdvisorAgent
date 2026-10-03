#include "llm_judge.hpp"

#include "absl/strings/str_cat.h"
#include "app/geminiclient/gemini_generation.hpp"
#include "nlohmann/json.hpp"

#include <cctype>

namespace {
std::string ExtractJudgeText(const std::string &raw) {
  const nlohmann::json root = nlohmann::json::parse(raw, nullptr, false);
  if (root.is_discarded() || !root.contains("candidates") ||
      !root.at("candidates").is_array() || root.at("candidates").empty()) {
    return raw;
  }
  const nlohmann::json &candidate = root.at("candidates").at(0);
  if (!candidate.contains("content")) {
    return raw;
  }
  if (candidate.at("content").is_object() &&
      candidate.at("content").contains("parts") &&
      candidate.at("content").at("parts").is_array() &&
      !candidate.at("content").at("parts").empty() &&
      candidate.at("content").at("parts").at(0).contains("text")) {
    return candidate.at("content").at("parts").at(0).at("text").get<std::string>();
  }
  return raw;
}

std::string NormalizeJsonPayload(std::string text) {
  auto trim = [](std::string *value) {
    while (!value->empty() &&
           std::isspace(static_cast<unsigned char>(value->front()))) {
      value->erase(value->begin());
    }
    while (!value->empty() &&
           std::isspace(static_cast<unsigned char>(value->back()))) {
      value->pop_back();
    }
  };
  trim(&text);
  const std::string fence = "```";
  if (text.rfind("```json", 0) == 0) {
    text.erase(0, 7);
  } else if (text.rfind(fence, 0) == 0) {
    text.erase(0, fence.size());
  }
  trim(&text);
  if (text.size() >= fence.size() &&
      text.compare(text.size() - fence.size(), fence.size(), fence) == 0) {
    text.erase(text.size() - fence.size());
  }
  trim(&text);
  const std::size_t first = text.find('{');
  const std::size_t last = text.rfind('}');
  if (first != std::string::npos && last != std::string::npos && last >= first) {
    return text.substr(first, last - first + 1);
  }
  return text;
}
} // namespace

EvalMetricResult RunLlmJudge(std::string_view query,
                             std::string_view advisor_text,
                             std::string_view evidence) {
  GeminiGenerator generator;
  const std::string prompt = absl::StrCat(
      "You are grading an academic advising assistant.\n"
      "Return JSON only: {\"pass\": true|false, \"helpfulness\": 0-1, "
      "\"faithfulness\": 0-1, \"actionability\": 0-1, \"reason\": \"...\"}\n"
      "Pass only if every score is at least 0.6.\n"
      "Penalize invented catalog facts that are not supported by evidence.\n\n"
      "User query:\n",
      query, "\n\nAdvisor answer:\n", advisor_text, "\n\nEvidence:\n",
      evidence.empty() ? "(none)" : evidence);
  const absl::Status status = generator.geminiGen(prompt, "lightweight");
  if (!status.ok()) {
    return {"llm_judge", false,
            absl::StrCat("llm judge request failed: ", status.ToString())};
  }
  const std::string payload =
      NormalizeJsonPayload(ExtractJudgeText(generator.getContent()));
  const nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return {"llm_judge", false, "llm judge returned unparseable JSON"};
  }
  const bool pass = parsed.contains("pass") && parsed.at("pass").is_boolean() &&
                    parsed.at("pass").get<bool>();
  const std::string reason =
      parsed.contains("reason") && parsed.at("reason").is_string()
          ? parsed.at("reason").get<std::string>()
          : (pass ? "llm judge passed" : "llm judge failed");
  return {"llm_judge", pass, reason};
}
