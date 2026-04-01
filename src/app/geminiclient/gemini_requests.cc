#include "../../../include/app/geminiclient/gemini_requests.hpp"

GeminiRequests::GeminiRequests() {
  char *check = std::getenv("GEMINI_API_KEY");
  apikey = (check == nullptr) ? "" : check;
  char *region = std::getenv("REGION");
  location = (region == nullptr) ? "" : region;
  char *project = std::getenv("GEMINI_API_KEY");
  project_id = (project == nullptr) ? "" : project;
}
GeminiRequests::~GeminiRequests() { apikey = ""; }
