#ifndef WEB_SCRAPER_TOOL
#define WEB_SCRAPER_TOOL

#include "tools.hpp"
#include <absl/status/status.h>
#include <absl/status/statusor.h>
class Scraper : public Tool {
private:
  std::string jinaapikey;
  bool json_format;
  std::string toolDescription;

public:
  Scraper();
  Scraper(bool json_on);
  absl::StatusOr<std::string> scrapeFromUrl(std::string_view url);
  std::string getToolDescription(); // implement
};

#endif