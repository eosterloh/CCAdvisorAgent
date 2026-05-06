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
constexpr const char *kDepartmentsIndexUrl =
    "https://coursecatalog.coloradocollege.edu/departments/";
constexpr const char *kCatalogHost =
    "https://coursecatalog.coloradocollege.edu";

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

std::string AcronymFromTokens(const std::vector<std::string> &tokens) {
  std::string acronym;
  for (const std::string &token : tokens) {
    if (!token.empty() &&
        std::isalpha(static_cast<unsigned char>(token.front())) != 0) {
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
      {"poli sci", "political science"},
      {"polisci", "political science"},
      {"polysci", "political science"},
      {"ir", "international political economy"},
      {"anthro", "anthropology"},
      {"chem", "chemistry"},
      {"phys", "physics"},
      {"phil", "philosophy"},
      {"theatre", "theatre dance"},
      {"theater", "theatre dance"},
  };
  for (const auto &entry : aliases) {
    if (normalized == entry.first) {
      return entry.second;
    }
  }
  return normalized;
}

// Classic dynamic-programming Levenshtein distance.
int LevenshteinDistance(std::string_view a, std::string_view b) {
  const std::size_t m = a.size();
  const std::size_t n = b.size();
  if (m == 0) {
    return static_cast<int>(n);
  }
  if (n == 0) {
    return static_cast<int>(m);
  }
  std::vector<int> prev(n + 1);
  std::vector<int> curr(n + 1);
  for (std::size_t j = 0; j <= n; ++j) {
    prev[j] = static_cast<int>(j);
  }
  for (std::size_t i = 1; i <= m; ++i) {
    curr[0] = static_cast<int>(i);
    for (std::size_t j = 1; j <= n; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, curr);
  }
  return prev[n];
}

// Returns similarity in [0.0, 1.0] using edit distance over max length.
double SimilarityRatio(std::string_view a, std::string_view b) {
  const std::size_t max_len = std::max(a.size(), b.size());
  if (max_len == 0) {
    return 1.0;
  }
  const int distance = LevenshteinDistance(a, b);
  return 1.0 - static_cast<double>(distance) / static_cast<double>(max_len);
}

struct Department {
  std::string code;
  std::string label;
  std::string overview_url;
};

struct DepartmentMatch {
  Department department;
  int score = 0;
  std::string reason;
};

int ScoreDepartment(std::string_view canonical_target,
                    const Department &candidate) {
  const std::string target_norm{canonical_target};
  const std::string code_lower = absl::AsciiStrToLower(candidate.code);
  const std::string label_norm = NormalizeText(candidate.label);
  const std::vector<std::string> target_tokens = Tokenize(target_norm);
  const std::vector<std::string> label_tokens = Tokenize(label_norm);
  const std::string target_acronym = AcronymFromTokens(target_tokens);
  const std::string label_acronym = AcronymFromTokens(label_tokens);

  int score = 0;
  if (target_norm == code_lower) {
    score += 25;
  }
  if (!target_norm.empty() &&
      label_norm.find(target_norm) != std::string::npos) {
    score += 15;
  }
  if (!target_acronym.empty() && target_acronym == label_acronym) {
    score += 10;
  }
  if (!target_acronym.empty() && target_acronym == code_lower) {
    score += 10;
  }
  for (const std::string &token : target_tokens) {
    if (token.size() < 3) {
      continue;
    }
    if (label_norm.find(token) != std::string::npos) {
      score += 4;
    }
  }
  // Typo-tolerance: contribute up to +12 by similarity to label or code.
  const double label_sim = SimilarityRatio(target_norm, label_norm);
  const double code_sim = SimilarityRatio(target_norm, code_lower);
  const double best_sim = std::max(label_sim, code_sim);
  if (best_sim >= 0.55) {
    score += static_cast<int>(best_sim * 12.0);
  }
  // Token-level fuzzy: best similarity of any user token vs any label token.
  double best_token_sim = 0.0;
  for (const std::string &t_target : target_tokens) {
    if (t_target.size() < 3) {
      continue;
    }
    for (const std::string &t_label : label_tokens) {
      if (t_label.size() < 3) {
        continue;
      }
      const double sim = SimilarityRatio(t_target, t_label);
      best_token_sim = std::max(best_token_sim, sim);
    }
  }
  if (best_token_sim >= 0.7) {
    score += static_cast<int>(best_token_sim * 6.0);
  }
  return score;
}

// Returns "Markdown content" payload string from Jina-shaped scrape JSON.
std::string ExtractScrapeBody(const std::string &payload_json) {
  const json parsed = json::parse(payload_json, nullptr, false);
  if (parsed.is_discarded()) {
    return "";
  }
  if (parsed.contains("data") && parsed["data"].is_object() &&
      parsed["data"].contains("content") &&
      parsed["data"]["content"].is_string()) {
    return parsed["data"]["content"].get<std::string>();
  }
  if (parsed.contains("content") && parsed["content"].is_string()) {
    return parsed["content"].get<std::string>();
  }
  return payload_json;
}

std::string JsonLineFromPayload(const std::string &payload_json) {
  const json parsed = json::parse(payload_json, nullptr, false);
  if (parsed.is_discarded()) {
    return "";
  }
  if (parsed.contains("data") && parsed["data"].is_object() &&
      parsed["data"].contains("content") &&
      parsed["data"]["content"].is_string()) {
    return parsed.dump();
  }
  if (parsed.contains("content") && parsed["content"].is_string()) {
    return parsed.dump();
  }
  return "";
}

absl::Status WriteJsonl(const fs::path &path,
                        const std::vector<std::string> &lines) {
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

// Parse the markdown departments index for [Label](.../departments/CODE/...) entries.
std::vector<Department> ParseDepartmentIndex(const std::string &body) {
  std::vector<Department> departments;
  std::set<std::string> seen_codes;
  const std::regex link_regex(
      R"(\[([^\]]+)\]\((https?://coursecatalog\.coloradocollege\.edu/departments/([A-Z]{2,6})(?:/[^)]*)?)\))");
  for (std::sregex_iterator it(body.begin(), body.end(), link_regex);
       it != std::sregex_iterator(); ++it) {
    const std::string label = (*it)[1].str();
    const std::string url = (*it)[2].str();
    const std::string code = (*it)[3].str();
    if (!seen_codes.insert(code).second) {
      continue;
    }
    departments.push_back(Department{code, label, url});
  }
  return departments;
}

absl::StatusOr<std::vector<Department>>
EnumerateDepartments(Scraper &scraper) {
  absl::StatusOr<std::string> page_or =
      scraper.scrapeFromUrl(kDepartmentsIndexUrl);
  if (!page_or.ok()) {
    return page_or.status();
  }
  const std::string body = ExtractScrapeBody(*page_or);
  std::vector<Department> departments = ParseDepartmentIndex(body);
  if (departments.empty()) {
    return absl::NotFoundError(
        "Departments index returned no parseable department links.");
  }
  return departments;
}

absl::StatusOr<DepartmentMatch>
MatchDepartment(std::string_view major_input,
                const std::vector<Department> &departments) {
  const std::string canonical = CanonicalizeMajorInput(std::string(major_input));
  std::vector<DepartmentMatch> matches;
  matches.reserve(departments.size());
  for (const Department &dept : departments) {
    DepartmentMatch m;
    m.department = dept;
    m.score = ScoreDepartment(canonical, dept);
    matches.push_back(std::move(m));
  }
  std::sort(matches.begin(), matches.end(),
            [](const DepartmentMatch &a, const DepartmentMatch &b) {
              return a.score > b.score;
            });
  if (matches.empty() || matches.front().score < 10) {
    std::string hint = "Could not confidently map input '";
    hint += std::string(major_input);
    hint += "' to a catalog department. Closest candidates: ";
    for (std::size_t i = 0; i < matches.size() && i < 3; ++i) {
      if (i > 0) {
        hint += ", ";
      }
      hint += matches[i].department.label;
      hint += " (";
      hint += matches[i].department.code;
      hint += ", score=";
      hint += std::to_string(matches[i].score);
      hint += ")";
    }
    return absl::NotFoundError(hint);
  }
  // Require a clear margin so we never silently pick the wrong dept.
  if (matches.size() > 1 &&
      matches.front().score - matches.at(1).score < 4) {
    std::string hint = "Department match for '";
    hint += std::string(major_input);
    hint += "' was ambiguous between: ";
    hint += matches.front().department.label;
    hint += " (" + matches.front().department.code + ")";
    hint += " and ";
    hint += matches.at(1).department.label;
    hint += " (" + matches.at(1).department.code + "). ";
    hint += "Re-run with a more specific name (e.g. include 'Department').";
    return absl::FailedPreconditionError(hint);
  }
  return matches.front();
}

std::vector<std::string>
ParseProgramLinksFromDeptPage(const std::string &body) {
  std::set<std::string> unique_urls;
  const std::regex prog_regex(
      R"(https?://coursecatalog\.coloradocollege\.edu/programs/[A-Za-z0-9\-]+)");
  for (std::sregex_iterator it(body.begin(), body.end(), prog_regex);
       it != std::sregex_iterator(); ++it) {
    unique_urls.insert((*it)[0].str());
  }
  // Also accept relative links like "/programs/COSC-BA-Major".
  const std::regex rel_regex(R"((?:^|[^a-zA-Z])(/programs/[A-Za-z0-9\-]+))");
  for (std::sregex_iterator it(body.begin(), body.end(), rel_regex);
       it != std::sregex_iterator(); ++it) {
    unique_urls.insert(absl::StrCat(kCatalogHost, (*it)[1].str()));
  }
  return std::vector<std::string>(unique_urls.begin(), unique_urls.end());
}

absl::StatusOr<std::vector<std::string>>
ListProgramsInDepartment(Scraper &scraper, const std::string &dept_code) {
  const std::string url =
      absl::StrCat(kCatalogHost, "/departments/", dept_code, "/programs");
  absl::StatusOr<std::string> page_or = scraper.scrapeFromUrl(url);
  if (!page_or.ok()) {
    return page_or.status();
  }
  const std::string body = ExtractScrapeBody(*page_or);
  std::vector<std::string> urls = ParseProgramLinksFromDeptPage(body);
  if (urls.empty()) {
    return absl::NotFoundError(absl::StrCat(
        "No /programs/ links found on department page: ", url));
  }
  return urls;
}

absl::Status EmbedAndPushFile(const fs::path &file_path,
                              std::string_view major_key,
                              std::string_view major_name,
                              GeminiEmbedding &embedding,
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

  // Step 1: enumerate all 35 departments from the catalog index.
  absl::StatusOr<std::vector<Department>> departments_or =
      EnumerateDepartments(scraper);
  if (!departments_or.ok()) {
    std::cerr << "Failed to enumerate departments: "
              << departments_or.status() << '\n';
    return 1;
  }
  std::cout << "Discovered " << departments_or->size()
            << " departments from catalog index.\n";

  // Step 2: fuzzy-match user's major input against the department list.
  absl::StatusOr<DepartmentMatch> match_or =
      MatchDepartment(major_name, *departments_or);
  if (!match_or.ok()) {
    std::cerr << "Department resolution failed: " << match_or.status() << '\n';
    return 1;
  }
  const Department resolved = match_or->department;
  std::cout << "Matched '" << major_name << "' -> " << resolved.label
            << " (" << resolved.code << ", score=" << match_or->score
            << ")\n";

  const fs::path working_dir =
      fs::path("data") / "major_ingest" / major_key / "raw";
  std::error_code mkdir_error;
  fs::create_directories(working_dir, mkdir_error);
  if (mkdir_error) {
    std::cerr << "Failed to create ingest directory: "
              << mkdir_error.message() << '\n';
    return 1;
  }

  // Step 3: fetch /departments/<CODE>/programs and follow each /programs/...
  // link to the actual major/minor page. Each page becomes one JSONL line.
  absl::StatusOr<std::vector<std::string>> program_urls_or =
      ListProgramsInDepartment(scraper, resolved.code);
  if (!program_urls_or.ok()) {
    std::cerr << "Failed to list programs in department " << resolved.code
              << ": " << program_urls_or.status() << '\n';
    return 1;
  }
  std::cout << "Found " << program_urls_or->size() << " program(s) in "
            << resolved.code << ".\n";

  std::vector<std::string> program_lines;
  for (const std::string &program_url : *program_urls_or) {
    absl::StatusOr<std::string> program_scrape_or =
        scraper.scrapeFromUrl(program_url);
    if (!program_scrape_or.ok()) {
      std::cerr << "  warn: scrape failed for " << program_url << ": "
                << program_scrape_or.status() << '\n';
      continue;
    }
    const std::string line = JsonLineFromPayload(*program_scrape_or);
    if (!line.empty()) {
      program_lines.push_back(line);
      std::cout << "  + " << program_url << '\n';
    }
  }
  const fs::path program_jsonl = working_dir / "program_requirements.jsonl";
  if (program_lines.empty()) {
    std::cerr << "No program payloads were captured for department "
              << resolved.code << ".\n";
    return 1;
  }
  absl::Status write_status = WriteJsonl(program_jsonl, program_lines);
  if (!write_status.ok()) {
    std::cerr << write_status << '\n';
    return 1;
  }

  // Step 4: scrape /departments/<CODE>/courses (single page works fine).
  const std::string courses_url = absl::StrCat(
      kCatalogHost, "/departments/", resolved.code, "/courses");
  std::vector<std::string> course_lines;
  absl::StatusOr<std::string> courses_scrape_or =
      scraper.scrapeFromUrl(courses_url);
  if (courses_scrape_or.ok()) {
    const std::string line = JsonLineFromPayload(*courses_scrape_or);
    if (!line.empty()) {
      course_lines.push_back(line);
    }
  } else {
    std::cerr << "  warn: courses scrape failed for " << courses_url << ": "
              << courses_scrape_or.status() << '\n';
  }
  const fs::path courses_jsonl = working_dir / "department_courses.jsonl";
  if (!course_lines.empty()) {
    write_status = WriteJsonl(courses_jsonl, course_lines);
    if (!write_status.ok()) {
      std::cerr << write_status << '\n';
      return 1;
    }
  }

  // Step 5: embed + push to Weaviate, stamping every chunk with the
  // user-provided major_key so retrieval can filter on it later.
  GeminiEmbedding embedding;
  weaviateClient weaviate;
  write_status = EmbedAndPushFile(program_jsonl, major_key, resolved.label,
                                  embedding, weaviate);
  if (!write_status.ok()) {
    std::cerr << "Failed to ingest program requirements: " << write_status
              << '\n';
    return 1;
  }
  if (!course_lines.empty()) {
    write_status = EmbedAndPushFile(courses_jsonl, major_key, resolved.label,
                                    embedding, weaviate);
    if (!write_status.ok()) {
      std::cerr << "Failed to ingest department course listings: "
                << write_status << '\n';
      return 1;
    }
  }

  std::cout << "Ingestion complete for major '" << major_name
            << "' mapped to department '" << resolved.label
            << "' (" << resolved.code << "), "
            << program_lines.size() << " program(s) and "
            << course_lines.size() << " course-page(s) written.\n";
  return 0;
}
