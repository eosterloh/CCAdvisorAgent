#include "tool_calling_tests.hpp"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/toolcalling/scraper_tool.hpp"

#include <iostream>

absl::Status RunToolCallingTests() {
  std::cout << "[ToolCalling] Running scraper JSON-mode test...\n";
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
