#ifndef RETRIEVE_FROM_WEAVIATE
#define RETRIEVE_FROM_WEAVIATE

#include "absl/status/statusor.h"
#include <string>
#include <string_view>

absl::StatusOr<std::string>
retrieveFromWeaviate(std::string_view query, std::string_view major_key = "",
                     std::string_view ingest_status_file = "");

#endif
