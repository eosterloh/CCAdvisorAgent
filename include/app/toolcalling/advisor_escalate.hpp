#ifndef ADVISOR_ESCALATE
#define ADVISOR_ESCALATE

#include <string>
#include <string_view>

inline constexpr const char *kAdvisorCalendarUrl =
    "https://www.coloradocollege.edu/offices/advising/";

// Build an out-of-scope escalation with a calendar signup link.
std::string BuildAdvisorEscalation(std::string_view advisor,
                                   std::string_view reason);

#endif
