#ifndef GEMINI_GENERATE
#define GEMINI_GENERATE

#include "absl/status/status.h"
#include "gemini_requests.hpp"

class GeminiGenerator : public GeminiRequests {
private:
  std::string msg_content;

public:
  GeminiGenerator();
  ~GeminiGenerator();
  absl::Status geminiGen(std::string_view query);
  absl::Status bindTool(std::string_view tool);
  std::string getContent() { return msg_content; }
};

#endif