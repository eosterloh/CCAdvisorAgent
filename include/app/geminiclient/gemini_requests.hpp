#ifndef GEMINI_REQUESTS
#define GEMINI_REQUESTS

#include <cstdlib>
#include <string>

class GeminiRequests {
protected:
  std::string apikey;
  std::string location;
  std::string project_id;

public:
  GeminiRequests();
  ~GeminiRequests();
};

#endif