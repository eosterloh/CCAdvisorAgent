#ifndef RERANKER
#define RERANKER

#include <absl/status/statusor.h>
#include <string>
#include <string_view>

class Reranker {
public:
  Reranker() = default;
  absl::StatusOr<std::string> rerank(std::string_view chunks,
                                     std::string_view query);
};

#endif