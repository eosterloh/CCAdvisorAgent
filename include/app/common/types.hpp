#ifndef CUSTOM_TYPES
#define CUSTOM_TYPES

#include <optional>
#include <string>
#include <vector>

struct EmbeddedRecord {
  std::string source_path; // filepath url
  std::string source_url;
  std::optional<std::string> course_code;
  std::string course_title;
  std::string chunk_text;
  std::vector<float> embedding;
};

#endif