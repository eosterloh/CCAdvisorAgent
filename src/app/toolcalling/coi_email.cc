#include "app/toolcalling/coi_email.hpp"
#include "absl/strings/str_cat.h"

std::string DraftCoiEmail(std::string_view advisor, std::string_view student_name,
                          std::string_view course, std::string_view missing_prereq,
                          std::string_view extra_context) {
  const std::string to = advisor.empty() ? "advisor@coloradocollege.edu"
                                         : std::string(advisor);
  const std::string name =
      student_name.empty() ? "a Colorado College student" : std::string(student_name);
  const std::string target =
      course.empty() ? "the requested course" : std::string(course);
  const std::string missing =
      missing_prereq.empty() ? "one listed prerequisite" : std::string(missing_prereq);

  std::string body = absl::StrCat(
      "To: ", to, "\n",
      "Subject: Consent of Instructor request for ", target, "\n\n",
      "Dear ", to, ",\n\n",
      "My name is ", name,
      ". I am requesting Consent of Instructor (COI) to enroll in ", target,
      ". I am one prerequisite away: I have not yet completed ", missing,
      ", but I have completed the rest of the listed preparation and believe I "
      "can succeed in the course.\n\n");
  if (!extra_context.empty()) {
    body = absl::StrCat(body, "Additional context:\n", extra_context, "\n\n");
  }
  body = absl::StrCat(
      body,
      "Would you be willing to grant COI, or advise me on the best next step "
      "before enrollment?\n\nThank you,\n",
      name, "\n");
  return body;
}
