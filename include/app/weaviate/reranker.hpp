#ifndef RERANKER
#define RERANKER

#include <absl/status/statusor.h>

class Reranker {
public:
  Reranker() = default;
  absl::StatusOr<std::string> rerank(std::string chunks, std::string query);
};

#endif