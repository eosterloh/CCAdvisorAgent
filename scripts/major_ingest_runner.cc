#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/geminiclient/gemini_embedding.hpp"
#include "app/toolcalling/scraper_tool.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr const char *kProgramsIndexUrl =
    "https://coursecatalog.coloradocollege.edu/programs/";

std::string NormalizeText(std::string value) {
  absl::AsciiStrToLower(&value);
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
      out.push_back(c);
    } else {
      out.push_back(' ');
    }
  }
  return out;
}

std::vector<std::string> Tokenize(std::string_view normalized) {
  std::vector<std::string> tokens;
  std::string current;
  for (char c : normalized) {
    if (c == ' ') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

std::string ParseMajorKeyFromProgramUrl(std::string_view program_url) {
  const std::regex key_regex("/programs/([A-Za-z0-9\\-]+)");
  std::smatch match;
  const std::string url(program_url);
  if (std::regex_search(url, match, key_regex) &&
      match.size() > 1) {
    return NormalizeText(match[1].str());
  }
  return "";
}

std::string JsonLineFromPayload(const std::string &payload_json) {
  const json parsed = json::parse(payload_json, nullptr, false);
  if (parsed.is_discarded()) {
    return "";
  }

  if (parsed.contains("data") && parsed["data"].is_object() &&
      parsed["data"].contains("content") && parsed["data"]["content"].is_string()) {
    return parsed.dump();
  }
  if (parsed.contains("content") && parsed["content"].is_string()) {
    return parsed.dump();
  }
  return "";
}

absl::Status WriteJsonl(const fs::path &path, const std::vector<std::string> &lines) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open file for writing: ", path.string()));
  }
  for (const std::string &line : lines) {
    out << line << '\n';
  }
  return absl::OkStatus();
}

std::vector<std::pair<std::string, std::string>>
ExtractProgramLinks(const std::string &programs_markdown) {
  std::vector<std::pair<std::string, std::string>> links;
  const std::regex link_regex(
      R"(\[([^\]]+)\]\((https?://coursecatalog\.coloradocollege\.edu/programs/[^)\s]+)\))");
  for (std::sregex_iterator it(programs_markdown.begin(), programs_markdown.end(),
                               link_regex);
       it != std::sregex_iterator(); ++it) {
    links.emplace_back((*it)[1].str(), (*it)[2].str());
  }
  return links;
}

std::string AcronymFromTokens(const std::vector<std::string> &tokens) {
  std::string acronym;
  for (const std::string &token : tokens) {
    if (!token.empty() && std::isalpha(static_cast<unsigned char>(token.front())) != 0) {
      acronym.push_back(token.front());
    }
  }
  return acronym;
}

std::string CanonicalizeMajorInput(std::string input) {
  std::string normalized = NormalizeText(std::move(input));
  const std::unordered_map<std::string, std::string> aliases = {
      {"cs", "computer science"},
      {"comp sci", "computer science"},
      {"computerscience", "computer science"},
      {"econ", "economics"},
      {"math", "mathematics"},
      {"bio", "biology"},
      {"psych", "psychology"},
  };
  for (const auto &entry : aliases) {
    if (normalized == entry.first) {
      return entry.second;
    }
  }
  return normalized;
}

struct ProgramMatch {
  std::string label;
  std::string url;
  int score;
};

int MatchScore(std::string_view canonical_major, std::string_view candidate_label,
               std::string_view candidate_url) {
  const std::string normalized_target = CanonicalizeMajorInput(std::string(canonical_major));
  const std::vector<std::string> target_tokens = Tokenize(normalized_target);
  const std::vector<std::string> label_tokens =
      Tokenize(NormalizeText(std::string(candidate_label)));
  const std::string target_acronym = AcronymFromTokens(target_tokens);
  const std::string label_acronym = AcronymFromTokens(label_tokens);
  const std::string candidate_key = ParseMajorKeyFromProgramUrl(candidate_url);
  const std::string candidate_blob =
      NormalizeText(absl::StrCat(candidate_label, " ", candidate_url, " ", candidate_key));

  int score = 0;
  if (!normalized_target.empty() && candidate_blob.find(normalized_target) != std::string::npos) {
    score += 12;
  }
  if (!candidate_key.empty() && normalized_target.find(candidate_key) != std::string::npos) {
    score += 6;
  }
  if (!target_acronym.empty() && target_acronym == label_acronym) {
    score += 8;
  }
  for (const std::string &token : target_tokens) {
    if (token.size() < 3) {
      continue;
    }
    if (candidate_blob.find(token) != std::string::npos) {
      score += 3;
    }
  }
  return score;
}

absl::StatusOr<std::pair<std::string, std::string>>
ResolveProgram(std::string_view major_input, Scraper &scraper) {
  absl::StatusOr<std::string> programs_or =
      scraper.scrapeFromUrl(kProgramsIndexUrl);
  if (!programs_or.ok()) {
    return programs_or.status();
  }
  const std::vector<std::pair<std::string, std::string>> links =
      ExtractProgramLinks(*programs_or);
  if (links.empty()) {
    return absl::NotFoundError("No program links found in programs index.");
  }

  std::vector<ProgramMatch> matches;
  matches.reserve(links.size());
  for (const auto &entry : links) {
    matches.push_back(
        ProgramMatch{entry.first, entry.second,
                     MatchScore(CanonicalizeMajorInput(std::string(major_input)),
                                entry.first, entry.second)});
  }
  std::sort(matches.begin(), matches.end(),
            [](const ProgramMatch &a, const ProgramMatch &b) {
              return a.score > b.score;
            });

  if (matches.empty() || matches.front().score < 8) {
    return absl::NotFoundError(
        "Could not confidently map major input to a catalog program URL. "
        "Try the full major name as listed in the catalog.");
  }
  if (matches.size() > 1 && (matches.front().score - matches.at(1).score) <= 1) {
    return absl::NotFoundError(absl::StrCat(
        "Major input was ambiguous. Top matches were: ", matches.front().label,
        ", ", matches.at(1).label, ". Please provide a more specific major."));
  }
  return std::make_pair(matches.front().label, matches.front().url);
}

std::set<std::string> ExtractDepartmentCourseUrls(const std::string &program_payload) {
  std::set<std::string> urls;
  const json parsed = json::parse(program_payload, nullptr, false);
  if (parsed.is_discarded()) {
    return urls;
  }
  std::string body;
  if (parsed.contains("data") && parsed["data"].is_object() &&
      parsed["data"].contains("content") && parsed["data"]["content"].is_string()) {
    body = parsed["data"]["content"].get<std::string>();
  } else if (parsed.contains("content") && parsed["content"].is_string()) {
    body = parsed["content"].get<std::string>();
  }
  if (body.empty()) {
    return urls;
  }

  const std::regex dept_regex(
      R"(https?://coursecatalog\.coloradocollege\.edu/departments/[A-Za-z0-9\-]+/courses)");
  for (std::sregex_iterator it(body.begin(), body.end(), dept_regex);
       it != std::sregex_iterator(); ++it) {
    urls.insert((*it)[0].str());
  }
  return urls;
}

absl::Status EmbedAndPushFile(const fs::path &file_path, std::string_view major_key,
                              std::string_view major_name, GeminiEmbedding &embedding,
                              weaviateClient &weaviate) {
  const absl::Status embed_status = embedding.embedFile(file_path.string());
  if (!embed_status.ok()) {
    return embed_status;
  }
  while (true) {
    absl::StatusOr<EmbeddedRecord> record_or = embedding.getContent();
    if (!record_or.ok()) {
      break;
    }
    record_or->major_key = std::string(major_key);
    record_or->major_name = std::string(major_name);
    const absl::Status push_status = weaviate.embed(*record_or);
    if (!push_status.ok()) {
      return push_status;
    }
  }
  return absl::OkStatus();
}
} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: major_ingest_runner <major_name> <major_key>\n";
    return 1;
  }
  const std::string major_name = argv[1];
  const std::string major_key = argv[2];

  Scraper scraper(true);
  absl::StatusOr<std::pair<std::string, std::string>> program_or =
      ResolveProgram(major_name, scraper);
  if (!program_or.ok()) {
    std::cerr << "Failed to resolve major program: " << program_or.status() << '\n';
    return 1;
  }
  const std::string resolved_program_name = program_or->first;
  const std::string program_url = program_or->second;

  absl::StatusOr<std::string> program_scrape_or = scraper.scrapeFromUrl(program_url);
  if (!program_scrape_or.ok()) {
    std::cerr << "Failed to scrape program page: " << program_scrape_or.status() << '\n';
    return 1;
  }

  const fs::path working_dir =
      fs::path("data") / "major_ingest" / major_key / "raw";
  std::error_code mkdir_error;
  fs::create_directories(working_dir, mkdir_error);
  if (mkdir_error) {
    std::cerr << "Failed to create ingest directory: " << mkdir_error.message() << '\n';
    return 1;
  }

  const fs::path program_jsonl = working_dir / "program_requirements.jsonl";
  const std::string program_line = JsonLineFromPayload(*program_scrape_or);
  if (program_line.empty()) {
    std::cerr << "Program scrape payload was not valid JSON.\n";
    return 1;
  }
  absl::Status write_status = WriteJsonl(program_jsonl, {program_line});
  if (!write_status.ok()) {
    std::cerr << write_status << '\n';
    return 1;
  }

  std::vector<std::string> course_lines;
  const std::set<std::string> course_urls = ExtractDepartmentCourseUrls(*program_scrape_or);
  for (const std::string &url : course_urls) {
    absl::StatusOr<std::string> course_scrape_or = scraper.scrapeFromUrl(url);
    if (!course_scrape_or.ok()) {
      continue;
    }
    const std::string line = JsonLineFromPayload(*course_scrape_or);
    if (!line.empty()) {
      course_lines.push_back(line);
    }
  }
  const fs::path courses_jsonl = working_dir / "department_courses.jsonl";
  if (!course_lines.empty()) {
    write_status = WriteJsonl(courses_jsonl, course_lines);
    if (!write_status.ok()) {
      std::cerr << write_status << '\n';
      return 1;
    }
  }

  GeminiEmbedding embedding;
  weaviateClient weaviate;
  write_status =
      EmbedAndPushFile(program_jsonl, major_key, resolved_program_name, embedding, weaviate);
  if (!write_status.ok()) {
    std::cerr << "Failed to ingest program requirements: " << write_status << '\n';
    return 1;
  }
  if (!course_lines.empty()) {
    write_status = EmbedAndPushFile(courses_jsonl, major_key, resolved_program_name,
                                    embedding, weaviate);
    if (!write_status.ok()) {
      std::cerr << "Failed to ingest department course listings: " << write_status
                << '\n';
      return 1;
    }
  }

  std::cout << "Ingestion complete for major '" << major_name
            << "' mapped to '" << resolved_program_name << "'.\n";
  return 0;
}
