#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "absl/strings/str_cat.h"
#include "app/weaviate/reranker.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

absl::StatusOr<std::string> retrieveFromWeaviate(std::string_view query) {
  weaviateClient client;
  absl::StatusOr<EmbeddedRecord> retrieved_or = client.retreive(query);
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
  result["flattened_text"] = candidate_text;
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