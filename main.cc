// always return a status or
// a status or is a way of returning a status
// or a datatype if all goes well.
// hense the or.

#include "absl/status/status.h"
#include "absl/log/log.h"

#include "tests/gemini_client_tests.hpp"
#include "tests/tool_calling_tests.hpp"
#include "tests/weaviate_client_tests.hpp"

#include "include/app/conversation/chat_management.hpp"

#include <iostream>
#include <string>

int main() {
  std::cout << "Running Gemini client tests...\n";
  const absl::Status gemini_status = RunGeminiClientTests();
  if (!gemini_status.ok()) {
    LOG(ERROR) << gemini_status;
    return 1;
  }

  std::cout << "Running tool-calling tests...\n";
  const absl::Status tool_status = RunToolCallingTests();
  if (!tool_status.ok()) {
    LOG(ERROR) << tool_status;
    return 1;
  }

  std::cout << "Running Weaviate client tests...\n";
  const absl::Status weaviate_status = RunWeaviateClientTests();
  if (!weaviate_status.ok()) {
    LOG(ERROR) << weaviate_status;
    return 1;
  }
  std::cout << "All selected tests passed.\n";

  std::cout << "\n=== Chat Sandbox ===\n";
  std::cout << "Type 'chat' for normal onboarding, 'test' for preset profile, "
               "or press Enter to exit: ";
  std::string launch_choice;
  std::getline(std::cin, launch_choice);
  if (launch_choice != "chat" && launch_choice != "test") {
    std::cout << "Skipping chat sandbox.\n";
    return 0;
  }

  std::cout << "Initiating chat...\n";
  chat_manager c;
  if (launch_choice == "test") {
    c.loadTestProfilePreset();
    std::cout << "Loaded temporary test profile preset.\n";
  }
  const absl::Status chat_status = c.chat(std::cin, std::cout);
  if (!chat_status.ok()) {
    LOG(ERROR) << chat_status;
    return 1;
  }
  return 0;
}