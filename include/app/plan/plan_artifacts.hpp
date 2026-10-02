#ifndef PLAN_ARTIFACTS
#define PLAN_ARTIFACTS

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <string>
#include <string_view>
#include <vector>

inline const std::vector<std::string> &RequiredAcademicPlanSections() {
  static const std::vector<std::string> kSections = {
      "# Academic Plan", "## Student Profile", "## Recommended Course Path",
      "## Risks and Open Questions", "## Next Actions"};
  return kSections;
}

bool HasRequiredPlanSections(std::string_view markdown);

absl::Status WriteAcademicPlanMarkdown(std::string_view markdown,
                                       std::string_view path);

absl::Status WriteSimplePdf(std::string_view text, std::string_view path);

absl::StatusOr<std::string>
EnsureAcademicPlanMarkdown(std::string markdown);

#endif
