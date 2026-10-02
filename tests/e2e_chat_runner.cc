#include "app/conversation/chat_management.hpp"

#include "absl/status/status.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main() {
  chat_manager manager;
  manager.loadTestProfilePreset();
  std::istringstream in(
      "Use catalog evidence to suggest three CS electives that align with "
      "Applied AI and summarize prereq fit.\n"
      "I am one prerequisite away from Topics in Computer Science: Applied AI. "
      "Please draft a COI email for Consent of Instructor.\n"
      "This question is out of scope and too complex for the agent. Please "
      "sign up on the calendar / talk to a human advisor.\n"
      "Please finalize my academic plan now.\n");
  std::ostringstream out;
  const absl::Status status = manager.chat(in, out);
  {
    std::ofstream transcript("/opt/cursor/artifacts/e2e_chat_transcript.txt");
    transcript << out.str();
  }
  std::cout << out.str() << "\nSTATUS=" << status << "\n";

  bool saw_retrieve = false;
  bool saw_coi = false;
  bool saw_esc = false;
  bool saw_plan = false;
  for (const TraceEvent &event : manager.getTraceEvents()) {
    if (event.phase == "tool" && event.success && event.tool_name.has_value()) {
      if (*event.tool_name == "retrieve_from_weaviate") {
        saw_retrieve = true;
      }
      if (*event.tool_name == "draft_coi_email") {
        saw_coi = true;
      }
      if (*event.tool_name == "escalate_to_advisor") {
        saw_esc = true;
      }
    }
    if (event.phase == "plan_generation" && event.success) {
      saw_plan = true;
    }
  }

  const bool plan_md = static_cast<bool>(std::ifstream("plan.md"));
  const bool plan_pdf = static_cast<bool>(std::ifstream("plan.pdf"));
  std::cout << "retrieve=" << saw_retrieve << " coi=" << saw_coi
            << " escalate=" << saw_esc << " plan=" << saw_plan
            << " plan.md=" << plan_md << " plan.pdf=" << plan_pdf << "\n";

  if (!status.ok() || !saw_retrieve || !saw_coi || !saw_esc || !saw_plan ||
      !plan_md || !plan_pdf) {
    return 1;
  }
  return 0;
}
