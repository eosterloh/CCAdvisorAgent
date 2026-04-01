#ifndef ADD_EMBEDDING
#define ADD_EMBEDDING

#include "app/common/types.hpp"
#include "absl/status/statusor.h"
#include <absl/status/status.h>
#include <string>

class weaviateClient {
private:
  std::string HashRecordKey(const EmbeddedRecord &record) const;

public:
  absl::Status embed(EmbeddedRecord e);
  absl::StatusOr<EmbeddedRecord> retreive(std::string query);
};

#endif