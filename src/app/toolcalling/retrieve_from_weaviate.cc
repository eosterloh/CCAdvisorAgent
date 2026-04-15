#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "absl/strings/str_cat.h"
#include "app/weaviate/reranker.hpp"
#include "app/weaviate/weaviate_port.hpp"

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
  if (!reranked_or.ok()) {
    // fallback to plain retrieval payload if reranker fails
    return candidate_text;
  }
  return *reranked_or;
}