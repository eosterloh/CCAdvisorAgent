#ifndef RETRIEVE_FROM_WEAVIATE
#define RETRIEVE_FROM_WEAVIATE

#include "absl/status/statusor.h"
#include <string>

absl::StatusOr<std::string> retrieveFromWeaviate(const std::string &query);

#endif
