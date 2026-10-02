#ifndef ADD_EMBEDDING
#define ADD_EMBEDDING

#include "absl/status/statusor.h"
#include "app/common/types.hpp"
#include <absl/status/status.h>
#include <string>
#include <string_view>
#include <vector>

class weaviateClient {
private:
  std::string HashRecordKey(const EmbeddedRecord &record) const;

public:
  absl::Status embed(const EmbeddedRecord &e);
  absl::StatusOr<EmbeddedRecord> retreive(std::string_view query,
                                          std::string_view major_key = "");
  absl::StatusOr<std::vector<EmbeddedRecord>>
  retrieveMany(std::string_view query, std::string_view major_key = "",
               int limit = 8);
};

#endif