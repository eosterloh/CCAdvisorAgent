#include "app/conversation/chat_management.hpp"
#include "app/plan/plan_artifacts.hpp"
#include "app/toolcalling/advisor_escalate.hpp"
#include "app/toolcalling/coi_email.hpp"
#include "evals/scoring.hpp"

#include "absl/status/status.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
  int failures = 0;
  auto expect = [&](bool ok, const std::string &msg) {
    if (!ok) {
      std::cerr << "FAIL: " << msg << '\n';
      ++failures;
    } else {
      std::cout << "PASS: " << msg << '\n';
    }
  };

  const std::string email =
      DraftCoiEmail("JaneDoe", "Erick O", "CP341 Applied AI",
                    "Design and Analysis of Algorithms", "one prereq short");
  expect(email.find("To: JaneDoe") != std::string::npos, "COI email has To");
  expect(email.find("Consent of Instructor") != std::string::npos,
         "COI email mentions COI");
  expect(email.find("CP341") != std::string::npos, "COI email names course");

  const std::string note =
      BuildAdvisorEscalation("JaneDoe", "out of scope financial aid question");
  expect(note.find(kAdvisorCalendarUrl) != std::string::npos,
         "escalation includes calendar URL");
  expect(note.find("JaneDoe") != std::string::npos, "escalation names advisor");

  const std::string good_plan =
      "# Academic Plan\n## Student Profile\n- Erick\n## Recommended Course "
      "Path\n- CP341\n## Risks and Open Questions\n- prereqs\n## Next Actions\n-"
      " verify\n";
  expect(HasRequiredPlanSections(good_plan), "plan sections detected");
  expect(!HasRequiredPlanSections("# nope"), "missing sections rejected");

  const fs::path tmp = fs::temp_directory_path() / "ccadvisor_plan_test.pdf";
  const absl::Status pdf_status = WriteSimplePdf(good_plan, tmp.string());
  expect(pdf_status.ok(), "pdf write ok");
  if (pdf_status.ok()) {
    std::ifstream in(tmp, std::ios::binary);
    std::string header(5, '\0');
    in.read(header.data(), 5);
    expect(header == "%PDF-", "pdf magic header");
    fs::remove(tmp);
  }

  chat_manager manager;
  TraceEvent event;
  event.phase = "planner";
  event.event_summary = "Planner generated plan.";
  event.success = true;
  event.latency = std::chrono::milliseconds(12);
  event.query_id = 1;
  event.timestamp = 0;
  manager.addTestTraceEvent(event);
  absl::StatusOr<bool> has_planner = HasPhase(manager, "planner");
  expect(has_planner.ok() && *has_planner, "HasPhase planner");
  absl::StatusOr<bool> under =
      PhaseLatencyUnder(manager, "planner", std::chrono::milliseconds(100));
  expect(under.ok() && *under, "PhaseLatencyUnder");
  absl::StatusOr<bool> critical = NoFailedCriticalPhase(manager);
  expect(critical.ok() && *critical, "NoFailedCriticalPhase");

  if (failures != 0) {
    std::cerr << failures << " expansion unit tests failed.\n";
    return 1;
  }
  std::cout << "All expansion unit tests passed.\n";
  return 0;
}
