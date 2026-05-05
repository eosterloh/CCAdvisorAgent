#include "tool_calling_tests.hpp"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "app/toolcalling/scraper_tool.hpp"
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>

using json = nlohmann::json;

absl::Status RunToolCallingTests() {
  std::cout << "[ToolCalling] Running retrieval fallback smoke test...\n";
  const std::filesystem::path status_path =
      std::filesystem::temp_directory_path() / "ccadvisor_ingest_status_test.json";
  {
    std::ofstream status_out(status_path);
    status_out << R"({"state":"running","message":"Ingestion is in progress."})";
  }
  absl::StatusOr<std::string> fallback_or =
      retrieveFromWeaviate("sample query", "computer_science", status_path.string());
  std::filesystem::remove(status_path);
  if (!fallback_or.ok()) {
    return absl::InternalError(absl::StrCat(
        "[ToolCalling] retrieveFromWeaviate fallback failed: ",
        fallback_or.status().ToString()));
  }
  const json fallback = json::parse(*fallback_or, nullptr, false);
  if (fallback.is_discarded() || !fallback.contains("ingestion_state") ||
      fallback.at("ingestion_state") != "running") {
    return absl::InternalError(
        "[ToolCalling] Retrieval fallback did not report running ingestion state.");
  }

  std::cout << "[ToolCalling] Running scraper JSON-mode test...\n";
  const char *jina_key = std::getenv("JINA_AI_API_KEY");
  if (jina_key == nullptr || std::string(jina_key).empty()) {
    std::cout << "[ToolCalling] Skipping scraper test because JINA_AI_API_KEY "
                 "is not set.\n";
    std::cout << "[ToolCalling] All tests passed.\n";
    return absl::OkStatus();
  }
  Scraper scraper(true);
  const std::string url =
      "https://coursecatalog.coloradocollege.edu/courses/CP115";
  absl::StatusOr<std::string> scrape_or = scraper.scrapeFromUrl(url);
  if (!scrape_or.ok()) {
    return absl::InternalError(
        absl::StrCat("[ToolCalling] Scraper request failed: ",
                     scrape_or.status().ToString()));
  }
  if (scrape_or->find("\"data\"") == std::string::npos) {
    return absl::InternalError(
        "[ToolCalling] Scraper response missing expected JSON data field.");
  }

  std::cout << "[ToolCalling] All tests passed.\n";
  return absl::OkStatus();
}
