#include "gemini_client_tests.hpp"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/geminiclient/gemini_embedding.hpp"
#include "app/geminiclient/gemini_generation.hpp"

#include <iostream>

namespace {
absl::Status TestGeminiGeneration() {
  GeminiGenerator generator;
  const absl::Status generation_status = generator.geminiGen(
      "Reply with exactly one short sentence about the sky.");
  if (!generation_status.ok()) {
    return generation_status;
  }
  if (generator.getContent().empty()) {
    return absl::InternalError("Gemini generation returned empty content.");
  }
  return absl::OkStatus();
}

absl::Status TestGeminiEmbedding() {
  GeminiEmbedding embedder;
  const absl::Status embed_status =
      embedder.embed("[source] test://unit\n[content] small embedding smoke test\n");
  if (!embed_status.ok()) {
    return embed_status;
  }

  absl::StatusOr<EmbeddedRecord> record_or = embedder.getContent();
  if (!record_or.ok()) {
    return record_or.status();
  }
  if (record_or->embedding.empty()) {
    return absl::InternalError("Embedding vector is empty.");
  }
  if (record_or->chunk_text.empty()) {
    return absl::InternalError("Embedded chunk text is empty.");
  }
  return absl::OkStatus();
}
} // namespace

absl::Status RunGeminiClientTests() {
  std::cout << "[Gemini] Running generation test...\n";
  const absl::Status generation_status = TestGeminiGeneration();
  if (!generation_status.ok()) {
    return absl::InternalError(
        absl::StrCat("[Gemini] Generation test failed: ",
                     generation_status.ToString()));
  }

  std::cout << "[Gemini] Running embedding test...\n";
  const absl::Status embedding_status = TestGeminiEmbedding();
  if (!embedding_status.ok()) {
    return absl::InternalError(
        absl::StrCat("[Gemini] Embedding test failed: ",
                     embedding_status.ToString()));
  }

  std::cout << "[Gemini] All tests passed.\n";
  return absl::OkStatus();
}
