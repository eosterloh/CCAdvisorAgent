#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "absl/strings/str_cat.h"
#include "app/weaviate/reranker.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include "nlohmann/json.hpp"
#include <fstream>

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
  absl::StatusOr<EmbeddedRecord> retrieved_or = client.retreive(query, major_key);
  if (!retrieved_or.ok()) {
    return retrieved_or.status();
  }

  const std::string candidate_text = absl::StrCat(
      "course_code: ",
      retrieved_or->course_code.has_value() ? *retrieved_or->course_code : "",
      "\n", "title: ", retrieved_or->course_title, "\n",
      "source_url: ", retrieved_or->source_url, "\n",
      "source_path: ", retrieved_or->source_path, "\n",
      "chunk_text: ", retrieved_or->chunk_text);

  Reranker reranker;
  absl::StatusOr<std::string> reranked_or =
      reranker.rerank(candidate_text, query);

  json result;
  result["query"] = query;
  result["major_key"] = major_key;
  result["flattened_text"] = candidate_text;
  result["ingestion_state"] = "ready";
  result["evidence"] = json::array(
      {json{{"source_url", retrieved_or->source_url},
            {"source_path", retrieved_or->source_path},
            {"course_code", retrieved_or->course_code.has_value()
                                ? *retrieved_or->course_code
                                : ""},
            {"title", retrieved_or->course_title},
            {"chunk_text", retrieved_or->chunk_text}}});
  if (reranked_or.ok()) {
    result["reranked_summary"] = *reranked_or;
  } else {
    // Keep retrieval grounded and structured even if reranker fails.
    result["reranked_summary"] = "";
    result["reranker_error"] = reranked_or.status().ToString();
  }
  return result.dump();
}