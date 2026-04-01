// always return a status or
// a status or is a way of returning a status
// or a datatype if all goes well.
// hense the or.

#include "absl/status/status.h"

#include "tests/gemini_client_tests.hpp"
#include "tests/tool_calling_tests.hpp"
#include "tests/weaviate_client_tests.hpp"

#include "include/app/conversation/chat_management.hpp"

#include <iostream>

int main() {
  std::cout << "Running Gemini client tests...\n";
  const absl::Status gemini_status = RunGeminiClientTests();
  if (!gemini_status.ok()) {
    std::cerr << gemini_status << "\n";
    return 1;
  }

  std::cout << "Running tool-calling tests...\n";
  const absl::Status tool_status = RunToolCallingTests();
  if (!tool_status.ok()) {
    std::cerr << tool_status << "\n";
    return 1;
  }

  std::cout << "Running Weaviate client tests...\n";
  const absl::Status weaviate_status = RunWeaviateClientTests();
  if (!weaviate_status.ok()) {
    std::cerr << weaviate_status << "\n";
    return 1;
  }
  std::cout << "All selected tests passed.\n";

  std::cout << "Intiating chat...";
  chat_manager c;
  c.getUserInfo(std::cin,
                std::cout); // Only for input (user typing) - for output
                            // (printing), use std::cout separately.
  c.chat(std::cin, std::cout);
  return 0;
}