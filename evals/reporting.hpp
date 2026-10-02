#ifndef EVAL_REPORTING
#define EVAL_REPORTING

#include "absl/status/status.h"
#include <string>
#include <vector>

struct EvalMetricResult {
  std::string name;
  bool pass = false;
  std::string reason;
};

struct EvalReportedCase {
  std::string id;
  bool passed = false;
  std::string reason;
  std::vector<std::string> tags;
  std::vector<EvalMetricResult> metrics;
};

std::string FormatEvalSummary(const std::vector<EvalReportedCase> &results);
absl::Status WriteEvalResultsJsonl(const std::string &path,
                                   const std::vector<EvalReportedCase> &results);

#endif
