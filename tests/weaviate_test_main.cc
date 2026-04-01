#include "absl/status/status.h"
#include "weaviate_client_tests.hpp"

#include <iostream>

int main() {
  std::cout << "Running Weaviate-only tests...\n";
  const absl::Status status = RunWeaviateClientTests();
  if (!status.ok()) {
    std::cerr << status << "\n";
    return 1;
  }

  std::cout << "Weaviate-only tests passed.\n";
  return 0;
}
