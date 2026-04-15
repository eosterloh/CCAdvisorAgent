#include "weaviate_client_tests.hpp"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include "cpr/cpr.h"
#include "nlohmann/json.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <vector>

using json = nlohmann::json;

namespace {

std::size_t DetectCourseChunkVectorDimension() {
  const std::string query =
      "{ Get { CourseChunk(limit: 1) { _additional { vector } } } }";
  cpr::Response response =
      cpr::Post(cpr::Url{"http://localhost:8080/v1/graphql"},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body{json({{"query", query}}).dump()});
  if (response.error || response.status_code >= 400) {
    return 3072;
  }

  const json parsed = json::parse(response.text, nullptr, false);
  if (parsed.is_discarded() || !parsed.contains("data") ||
      !parsed["data"].contains("Get") ||
      !parsed["data"]["Get"].contains("CourseChunk") ||
      !parsed["data"]["Get"]["CourseChunk"].is_array() ||
      parsed["data"]["Get"]["CourseChunk"].empty()) {
    return 3072;
  }

  const json &first = parsed["data"]["Get"]["CourseChunk"].at(0);
  if (!first.contains("_additional") ||
      !first["_additional"].contains("vector") ||
      !first["_additional"]["vector"].is_array()) {
    return 3072;
  }

  const std::size_t detected_dim = first["_additional"]["vector"].size();
  return detected_dim == 0 ? 3072 : detected_dim;
}

std::string RandomSuffix() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  std::ostringstream out;
  out << std::hex << dist(gen);
  return out.str();
}

} // namespace

absl::Status RunWeaviateClientTests() {
  std::cout << "[Weaviate] Running embed + nearVector retrieve test...\n";

  weaviateClient client;
  const std::string run_id = RandomSuffix();
  EmbeddedRecord record;
  record.source_path = absl::StrCat("tests://weaviate_client_tests/", run_id);
  record.source_url = absl::StrCat("https://example.test/course/CP999/", run_id);
  record.course_code = std::optional<std::string>("CP999");
  record.course_title = "Synthetic Test Course";
  record.chunk_text = absl::StrCat(
      "This synthetic chunk is for testing near vector retrieval. run_id=",
      run_id);
  const std::size_t embedding_dim = DetectCourseChunkVectorDimension();
  record.embedding = std::vector<float>(embedding_dim, 0.0F);
  if (embedding_dim > 0) {
    record.embedding.at(0) = 0.11F;
  }
  if (embedding_dim > 1) {
    record.embedding.at(1) = -0.22F;
  }
  if (embedding_dim > 2) {
    record.embedding.at(2) = 0.33F;
  }
  if (embedding_dim > 3) {
    record.embedding.at(3) = -0.44F;
  }

  const absl::Status embed_status = client.embed(record);
  if (!embed_status.ok()) {
    return absl::InternalError(
        absl::StrCat("[Weaviate] embed() failed: ", embed_status.ToString()));
  }

  std::ostringstream vector_stream;
  for (std::size_t i = 0; i < record.embedding.size(); ++i) {
    if (i > 0) {
      vector_stream << ",";
    }
    vector_stream << record.embedding.at(i);
  }

  const std::string graphql_query = absl::StrCat(
      "{ Get { CourseChunk(nearVector: {vector: [", vector_stream.str(),
      "]}, limit: 1) { course_code title source_url source_path chunk_text } "
      "} }");

  cpr::Response graphql_response =
      cpr::Post(cpr::Url{"http://localhost:8080/v1/graphql"},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body{json({{"query", graphql_query}}).dump()});
  if (graphql_response.error || graphql_response.status_code >= 400) {
    return absl::InternalError(
        absl::StrCat("[Weaviate] GraphQL nearVector request failed: ",
                     graphql_response.error.message));
  }

  const json parsed = json::parse(graphql_response.text, nullptr, false);
  if (parsed.is_discarded() || !parsed.contains("data") ||
      !parsed["data"].contains("Get") ||
      !parsed["data"]["Get"].contains("CourseChunk") ||
      !parsed["data"]["Get"]["CourseChunk"].is_array() ||
      parsed["data"]["Get"]["CourseChunk"].empty()) {
    return absl::InternalError(
        "[Weaviate] nearVector query returned no CourseChunk records.");
  }

  const json &first = parsed["data"]["Get"]["CourseChunk"].at(0);
  if (!first.contains("chunk_text") || !first["chunk_text"].is_string() ||
      first["chunk_text"].get<std::string>().empty()) {
    return absl::InternalError("[Weaviate] Retrieved chunk_text is empty.");
  }

  const char *gemini_key = std::getenv("GEMINI_API_KEY");
  if (gemini_key != nullptr && std::string(gemini_key).size() > 0) {
    absl::StatusOr<EmbeddedRecord> retrieve_or =
        client.retreive("Find a synthetic test course chunk.");
    if (!retrieve_or.ok()) {
      return absl::InternalError(
          absl::StrCat("[Weaviate] retreive() failed: ",
                       retrieve_or.status().ToString()));
    }
    if (retrieve_or->chunk_text.empty()) {
      return absl::InternalError("[Weaviate] retreive() chunk_text is empty.");
    }
  } else {
    std::cout << "[Weaviate] Skipping weaviateClient::retreive() because "
                 "GEMINI_API_KEY is not set.\n";
  }

  std::cout << "[Weaviate] All tests passed.\n";
  return absl::OkStatus();
}
