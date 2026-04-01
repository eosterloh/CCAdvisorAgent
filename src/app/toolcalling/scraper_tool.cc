#include "../../../include/app/toolcalling/scraper_tool.hpp"

// will be refactored into real_time_scraper_tool.cc
// This is a general scraping to not only understand the
//  basics of webscraping especially in c++ but also
//  to gather data
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpr/api.h"
#include "cpr/cprtypes.h"

#include <cpr/cpr.h>

Scraper::Scraper() {
  char *check = std::getenv("JINA_AI_API_KEY");
  jinaapikey = (check == nullptr) ? "" : check;
}

Scraper::Scraper(bool json_on) {
  char *check = std::getenv("JINA_AI_API_KEY");
  jinaapikey = (check == nullptr) ? "" : check;
  json_format = json_on;
}

absl::StatusOr<std::string> Scraper::scrapeFromUrl(std::string_view url) {
  if (jinaapikey == "") {
    return absl::InternalError("API key incorrectly read from .env");
  }
  std::string newurl = absl::StrCat("https://r.jina.ai/", url);
  cpr::Url u{newurl};
  cpr::Header h;
  if (json_format) {
    h = {{"Accept", "application/json"},
         {"Authorization", absl::StrCat("Bearer ", jinaapikey)}};
  } else {
    h = {{"Authorization", absl::StrCat("Bearer ", jinaapikey)}};
  }

  cpr::Response r = cpr::Get(u, h);
  if (r.status_code != 200) {
    return absl::InternalError(
        "Something went wrong with API call in the webscraper");
  } else {
    return r.text;
  }
}
