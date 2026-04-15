#ifndef GEMINI_EMBEDDING
#define GEMINI_EMBEDDING

#include "absl/status/statusor.h"
#include "app/common/types.hpp"
#include "gemini_requests.hpp"
#include <absl/status/status.h>
#include <vector>
class GeminiEmbedding : public GeminiRequests {
  // We are going to embed one at a time
  // Use one object: make sure to wipe it after every
  // get info
private:
  std::vector<EmbeddedRecord> embedded_records;

public:
  GeminiEmbedding();
  absl::Status embed(std::string_view chunk); // both sends the file to the
                                              // files api and embedds.
  absl::Status embedFile(std::string_view filepath);
  absl::StatusOr<EmbeddedRecord> getContent();
  absl::Status clean();
};

#endif