#include "app/toolcalling/advisor_escalate.hpp"
#include "absl/strings/str_cat.h"

std::string BuildAdvisorEscalation(std::string_view advisor,
                                   std::string_view reason) {
  const std::string who =
      advisor.empty() ? "your assigned advisor" : std::string(advisor);
  const std::string why =
      reason.empty()
          ? "this question is out of scope or too complex for automated advising"
          : std::string(reason);
  return absl::StrCat(
      "This question should be handled by a human advisor.\n",
      "Advisor: ", who, "\n",
      "Reason: ", why, "\n",
      "Next step: sign up on the advising calendar at ", kAdvisorCalendarUrl,
      " and bring this conversation summary to the meeting.\n");
}
