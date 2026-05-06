#include "../../../include/app/weaviate/weaviate_port.hpp"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "app/common/types.hpp"
#include "app/geminiclient/gemini_embedding.hpp"
#include <absl/strings/str_cat.h>
#include <cpr/cpr.h>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {
std::string EscapeGraphqlString(std::string_view input) {
  std::string escaped;
  escaped.reserve(input.size());
  for (char c : input) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  return escaped;
}
constexpr int kHttpTimeoutMs = 15000;
} // namespace

std::string weaviateClient::HashRecordKey(const EmbeddedRecord &record) const {
  const std::string key =
      absl::StrCat(record.source_url, "|", record.chunk_text);
  const std::size_t h1 = std::hash<std::string>{}(key);
  const std::size_t h2 =
      std::hash<std::string>{}(absl::StrCat("ccadvisor|", key));

  std::ostringstream stream;
  stream << std::hex << h1 << h2;
  std::string hex = stream.str();

  if (hex.size() < 32) {
    hex.append(32 - hex.size(), '0');
  } else if (hex.size() > 32) {
    hex = hex.substr(0, 32);
  }

  return absl::StrCat(hex.substr(0, 8), "-", hex.substr(8, 4), "-",
                      hex.substr(12, 4), "-", hex.substr(16, 4), "-",
                      hex.substr(20, 12));
}

absl::Status weaviateClient::embed(const EmbeddedRecord &e) {
  cpr::Url endpoint = "http://localhost:8080/v1/objects";
  const std::string object_id = HashRecordKey(e);

  json payload = {{"id", object_id},
                  {"class", "CourseChunk"},
                  {"properties",
                   {{"major_key", e.major_key},
                    {"major_name", e.major_name},
                    {"course_code", e.course_code},
                    {"title", e.course_title},
                    {"source_url", e.source_url},
                    {"source_path", e.source_path},
                    {"chunk_text", e.chunk_text}}},
                  {"vector", e.embedding}};
  cpr::Body body = payload.dump();

  cpr::Header header{{"Content-Type", "application/json"}};

  cpr::Response r = cpr::Post(endpoint, header, body,
                              cpr::Timeout{kHttpTimeoutMs});

  if (r.error || r.status_code >= 400) {
    LOG(ERROR) << "Weaviate embed request failed. HTTP " << r.status_code
               << ", transport error: " << r.error.message
               << ", response body: " << r.text;
    std::string message = absl::StrCat("Sending embeddings to database "
                                       "failed: ",
                                       r.error.message, " (HTTP ",
                                       r.status_code, ") ", "body=", r.text);
    return absl::InternalError(message);
  } else {
    return absl::OkStatus();
  }
}

absl::StatusOr<EmbeddedRecord> weaviateClient::retreive(std::string_view query,
                                                        std::string_view major_key) {
  GeminiEmbedding embedder;
  const absl::Status embed_status = embedder.embed(query);
  if (!embed_status.ok()) {
    return embed_status;
  }

  absl::StatusOr<EmbeddedRecord> query_record_or = embedder.getContent();
  if (!query_record_or.ok()) {
    return query_record_or.status();
  }
  if (query_record_or->embedding.empty()) {
    return absl::InternalError("Query embedding was empty.");
  }

  // Take vector turn it into comma seperated string to send to nearVector
  std::ostringstream vector_stream;
  for (std::size_t i = 0; i < query_record_or->embedding.size(); ++i) {
    if (i > 0) {
      vector_stream << ",";
    }
    vector_stream << query_record_or->embedding.at(i);
  }

  cpr::Url endpoint{"http://localhost:8080/v1/graphql"};
  cpr::Header header{{"Content-Type", "application/json"}};

  auto run_query = [&](const std::string &where_clause)
      -> absl::StatusOr<json> {
    const std::string graphql_query = absl::StrCat(
        "{ Get { CourseChunk(nearVector: {vector: [", vector_stream.str(),
        "]}", where_clause,
        ", limit: 10) { major_key major_name course_code title source_url "
        "source_path chunk_text _additional { id distance } } } }");
    json payload = {{"query", graphql_query}};
    cpr::Response r = cpr::Post(endpoint, header, cpr::Body{payload.dump()},
                                cpr::Timeout{kHttpTimeoutMs});
    if (r.error || r.status_code >= 400) {
      LOG(ERROR) << "Weaviate GraphQL request failed. HTTP " << r.status_code
                 << ", transport error: " << r.error.message
                 << ", response body: " << r.text;
      return absl::InternalError(
          absl::StrCat("GraphQL query failed: ", r.error.message, " (HTTP ",
                       r.status_code, ") body=", r.text));
    }
    return json::parse(r.text, nullptr, false);
  };

  auto has_results = [](const json &response_json) {
    return !response_json.is_discarded() && response_json.contains("data") &&
           response_json["data"].contains("Get") &&
           response_json["data"]["Get"].contains("CourseChunk") &&
           response_json["data"]["Get"]["CourseChunk"].is_array() &&
           !response_json["data"]["Get"]["CourseChunk"].empty();
  };

  std::string where_clause;
  if (!major_key.empty() && major_key != "undeclared") {
    where_clause = absl::StrCat(
        ", where: {path: [\"major_key\"], operator: Equal, valueText: \"",
        EscapeGraphqlString(major_key), "\"}");
  }

  absl::StatusOr<json> filtered_or = run_query(where_clause);
  if (!filtered_or.ok()) {
    return filtered_or.status();
  }
  json response_json = *std::move(filtered_or);

  // If a major filter was applied but returned nothing, retry unfiltered so
  // retrieval still produces evidence for users whose major has not ingested.
  if (!has_results(response_json) && !where_clause.empty()) {
    static std::set<std::string> already_warned_majors;
    const std::string key{major_key};
    if (already_warned_majors.insert(key).second) {
      LOG(WARNING)
          << "Weaviate filtered query returned no chunks for major_key="
          << key
          << "; falling back to unfiltered retrieval (will not warn again).";
    }
    absl::StatusOr<json> fallback_or = run_query(/*where_clause=*/"");
    if (!fallback_or.ok()) {
      return fallback_or.status();
    }
    response_json = *std::move(fallback_or);
  }

  if (!has_results(response_json)) {
    LOG(WARNING) << "Weaviate returned no CourseChunk objects for query.";
    return absl::InternalError("No CourseChunk objects found.");
  }

  const json &obj = response_json["data"]["Get"]["CourseChunk"].at(0);
  EmbeddedRecord record;
  record.source_path =
      obj.contains("source_path") && obj["source_path"].is_string()
          ? obj["source_path"].get<std::string>()
          : "";

  record.source_url =
      obj.contains("source_url") && obj["source_url"].is_string()
          ? obj["source_url"].get<std::string>()
          : "";
  record.major_key =
      obj.contains("major_key") && obj["major_key"].is_string()
          ? obj["major_key"].get<std::string>()
          : "";
  record.major_name =
      obj.contains("major_name") && obj["major_name"].is_string()
          ? obj["major_name"].get<std::string>()
          : "";

  if (obj.contains("course_code") && obj["course_code"].is_string()) {
    record.course_code = obj["course_code"].get<std::string>();
  } else {
    record.course_code = std::nullopt;
  }

  record.course_title = obj.contains("title") && obj["title"].is_string()
                            ? obj["title"].get<std::string>()
                            : "";

  record.chunk_text =
      obj.contains("chunk_text") && obj["chunk_text"].is_string()
          ? obj["chunk_text"].get<std::string>()
          : "";

  record.embedding.clear();

  return record;
}
