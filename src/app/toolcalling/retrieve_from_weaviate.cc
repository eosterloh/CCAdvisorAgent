#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "absl/strings/str_cat.h"
#include "app/weaviate/reranker.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <vector>

using json = nlohmann::json;

absl::StatusOr<std::string>
retrieveFromWeaviate(std::string_view query, std::string_view major_key,
                     std::string_view ingest_status_file) {
  if (!ingest_status_file.empty()) {
    std::ifstream status_in{std::string(ingest_status_file)};
    if (status_in.is_open()) {
      json status_json = json::parse(status_in, nullptr, false);
      if (!status_json.is_discarded() && status_json.contains("state") &&
          status_json["state"].is_string()) {
        const std::string state = status_json["state"].get<std::string>();
        if (state != "ready") {
          json pending;
          pending["query"] = query;
          pending["major_key"] = major_key;
          pending["ingestion_state"] = state;
          pending["flattened_text"] =
              "Major data is still populating. Retrieval used a partial fallback.";
          pending["evidence"] = json::array();
          pending["reranked_summary"] = "";
          if (status_json.contains("message") && status_json["message"].is_string()) {
            pending["ingestion_message"] = status_json["message"].get<std::string>();
          }
          return pending.dump();
        }
      }
    }
  }

  weaviateClient client;
  absl::StatusOr<std::vector<EmbeddedRecord>> retrieved_or =
      client.retrieveMany(query, major_key, 8);
  if (!retrieved_or.ok()) {
    return retrieved_or.status();
  }

  json evidence = json::array();
  std::string flattened;
  for (const EmbeddedRecord &record : *retrieved_or) {
    const std::string candidate_text = absl::StrCat(
        "course_code: ",
        record.course_code.has_value() ? *record.course_code : "", "\n",
        "title: ", record.course_title, "\n",
        "source_url: ", record.source_url, "\n",
        "source_path: ", record.source_path, "\n",
        "chunk_text: ", record.chunk_text, "\n---\n");
    flattened = absl::StrCat(flattened, candidate_text);
    evidence.push_back(
        json{{"source_url", record.source_url},
             {"source_path", record.source_path},
             {"course_code",
              record.course_code.has_value() ? *record.course_code : ""},
             {"title", record.course_title},
             {"chunk_text", record.chunk_text}});
  }

  Reranker reranker;
  absl::StatusOr<std::string> reranked_or = reranker.rerank(flattened, query);

  json result;
  result["query"] = query;
  result["major_key"] = major_key;
  result["flattened_text"] = flattened;
  result["ingestion_state"] = "ready";
  result["evidence"] = evidence;
  if (reranked_or.ok()) {
    result["reranked_summary"] = *reranked_or;
  } else {
    result["reranked_summary"] = "";
    result["reranker_error"] = reranked_or.status().ToString();
  }
  return result.dump();
}