#ifndef COI_EMAIL
#define COI_EMAIL

#include <string>
#include <string_view>

// Draft a Consent of Instructor email when the student is one prerequisite
// away from a target course.
std::string DraftCoiEmail(std::string_view advisor, std::string_view student_name,
                          std::string_view course, std::string_view missing_prereq,
                          std::string_view extra_context);

#endif
