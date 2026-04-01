#include "../../../include/app/geminiclient/gemini_embedding.hpp"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "app/geminiclient/gemini_requests.hpp"
#include <absl/status/status.h>
#include <cpr/cpr.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

GeminiEmbedding::GeminiEmbedding() { GeminiRequests{}; }

namespace {
std::optional<std::string> ExtractCourseCodeFromUrl(const std::string& source_url) {
  static const std::regex kCourseRegex("/courses/([A-Z]{2,4}[0-9]{3}(?:\\.[0-9]+)?)");
  std::smatch match;
  if (std::regex_search(source_url, match, kCourseRegex) && match.size() > 1) {
    return match[1].str();
  }
  return std::nullopt;
}

std::optional<std::string> ExtractCourseCodeFromContent(
    const std::string& content_text) {
  static const std::regex kContentCodeRegex(
      R"(#\s*([A-Z]{2,4}[0-9]{3}(?:\.[0-9]+)?)\s*\n\s*##\s*Download as PDF)");
  std::smatch match;
  if (std::regex_search(content_text, match, kContentCodeRegex) &&
      match.size() > 1) {
    return match[1].str();
  }
  return std::nullopt;
}

std::string ExtractCourseTitleFromContent(const std::string& content_text) {
  static const std::regex kTitleRegex(
      R"(##\s*Download as PDF\s*\n\s*##\s*([^\n]+))");
  std::smatch match;
  if (std::regex_search(content_text, match, kTitleRegex) && match.size() > 1) {
    return match[1].str();
  }
  return "";
}
} // namespace

absl::Status GeminiEmbedding::embed(std::string chunk) {
  if (apikey.empty()) {
    return absl::InternalError("GEMINI_API_KEY is not set.");
  }
  if (chunk.empty()) {
    return absl::InvalidArgumentError("Cannot embed an empty chunk.");
  }

  const std::string url = absl::StrCat(
      "https://generativelanguage.googleapis.com/v1beta/models/"
      "gemini-embedding-2-preview:embedContent?key=",
      apikey);

  const json payload = {{"content", {{"parts", {{{"text", chunk}}}}}}};

  const cpr::Response r = cpr::Post(
      cpr::Url{url}, cpr::Header{{"Content-Type", "application/json"}},
      cpr::Body{payload.dump()});

  if (r.status_code != 200) {
    std::cerr << "Error: " << r.status_code << " " << r.text << std::endl;
    return absl::InternalError("Embedding request failed.");
  }

  const json response_json = json::parse(r.text, nullptr, false);
  if (response_json.is_discarded()) {
    return absl::InternalError("Could not parse embedding response JSON.");
  }
  if (!response_json.contains("embedding") ||
      !response_json["embedding"].contains("values") ||
      !response_json["embedding"]["values"].is_array()) {
    return absl::InternalError("Embedding response missing embedding.values.");
  }

  const json& values_json = response_json["embedding"]["values"];
  std::vector<float> current_embedding;
  current_embedding.reserve(values_json.size());
  for (json::const_iterator it = values_json.begin(); it != values_json.end();
       ++it) {
    if (!it->is_number()) {
      return absl::InternalError("Embedding values contain non-numeric data.");
    }
    current_embedding.push_back(it->get<float>());
  }

  EmbeddedRecord record;
  record.source_path = "";
  record.source_url = "";
  record.course_code = std::nullopt;
  record.course_title = "";
  record.chunk_text = chunk;
  record.embedding = current_embedding;
  embedded_records.push_back(record);
  return absl::OkStatus();
}

absl::Status GeminiEmbedding::embedFile(std::string filepath) {
  std::ifstream f(filepath);
  if (!f.is_open()) {
    return absl::InternalError("Error opening file");
  }
  const bool is_course_file = (filepath.find("courses") != std::string::npos);

  std::string curline;
  bool embedded_at_least_one = false;
  std::size_t line_number = 0;
  while (std::getline(f, curline)) {
    ++line_number;
    if (curline.empty()) {
      continue;
    }

    const json res = json::parse(curline, nullptr, false);
    if (res.is_discarded()) {
      return absl::InternalError(
          absl::StrCat("Invalid JSON on line ", line_number, " in ", filepath));
    }

    std::string source_text;
    std::string content_text;

    if (res.contains("data") && res["data"].is_object()) {
      if (res["data"].contains("url") && res["data"]["url"].is_string()) {
        source_text = res["data"]["url"].get<std::string>();
      }
      if (res["data"].contains("content") && res["data"]["content"].is_string()) {
        content_text = res["data"]["content"].get<std::string>();
      }
    } else {
      if (res.contains("url") && res["url"].is_string()) {
        source_text = res["url"].get<std::string>();
      }
      if (res.contains("content") && res["content"].is_string()) {
        content_text = res["content"].get<std::string>();
      }
    }

    if (content_text.empty()) {
      return absl::InternalError(
          absl::StrCat("Missing content on line ", line_number, " in ", filepath));
    }

    const std::string chunk = absl::StrCat(
        "[source] ", source_text, "\n", "[content] ", content_text, "\n");
    if (apikey.empty()) {
      return absl::InternalError("GEMINI_API_KEY is not set.");
    }
    const std::string url = absl::StrCat(
        "https://generativelanguage.googleapis.com/v1beta/models/"
        "gemini-embedding-2-preview:embedContent?key=",
        apikey);

    const json payload = {{"content", {{"parts", {{{"text", chunk}}}}}}};
    const cpr::Response r = cpr::Post(
        cpr::Url{url}, cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{payload.dump()});
    if (r.status_code != 200) {
      return absl::InternalError(
          absl::StrCat("Embedding request failed on line ", line_number, "."));
    }

    const json response_json = json::parse(r.text, nullptr, false);
    if (response_json.is_discarded() || !response_json.contains("embedding") ||
        !response_json["embedding"].contains("values") ||
        !response_json["embedding"]["values"].is_array()) {
      return absl::InternalError(
          absl::StrCat("Malformed embedding response on line ", line_number, "."));
    }

    const json& values_json = response_json["embedding"]["values"];
    std::vector<float> current_embedding;
    current_embedding.reserve(values_json.size());
    for (json::const_iterator it = values_json.begin(); it != values_json.end();
         ++it) {
      if (!it->is_number()) {
        return absl::InternalError(
            absl::StrCat("Non-numeric embedding value on line ", line_number, "."));
      }
      current_embedding.push_back(it->get<float>());
    }

    EmbeddedRecord record;
    record.source_path = filepath;
    record.source_url = source_text;
    if (is_course_file) {
      std::optional<std::string> code = ExtractCourseCodeFromContent(content_text);
      if (!code.has_value()) {
        code = ExtractCourseCodeFromUrl(source_text);
      }
      record.course_code = code;
      record.course_title = ExtractCourseTitleFromContent(content_text);
    } else {
      record.course_code = std::nullopt;
      record.course_title = "";
    }
    record.chunk_text = chunk;
    record.embedding = current_embedding;
    embedded_records.push_back(record);
    embedded_at_least_one = true;
  }

  if (!embedded_at_least_one) {
    return absl::InternalError("No embeddable content found in file.");
  }
  return absl::OkStatus();
}

absl::StatusOr<EmbeddedRecord> GeminiEmbedding::getContent() {
  if (embedded_records.empty()) {
    return absl::InternalError(
        "Get content called with empty object embeddings");
  }
  EmbeddedRecord top = embedded_records.back();
  embedded_records.pop_back();
  if (top.embedding.empty() || top.chunk_text.empty()) {
    return absl::InternalError(
        "Get content called with empty object embeddings");
  }
  return top;
}

absl::Status GeminiEmbedding::clean() {
  embedded_records.clear();
  return absl::OkStatus();
}
