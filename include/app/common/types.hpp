#ifndef CUSTOM_TYPES
#define CUSTOM_TYPES

#include <chrono>
#include <optional>
#include <string>
#include <vector>

struct EmbeddedRecord {
  std::vector<float> embedding;
  std::string source_path; // filepath url
  std::string source_url;
  std::optional<std::string> course_code;
  std::string course_title;
  std::string chunk_text;
};

struct ToolCallingLogs {
  std::string toolname;
  std::string summary;
  int latency_ms;
  bool success;
};

struct TraceEvent {
  std::string phase;
  std::string event_summary;
  std::optional<std::string> tool_name;
  std::chrono::milliseconds latency;
  int query_id;
  int timestamp;
  bool success;
};

struct Test {};

#endif