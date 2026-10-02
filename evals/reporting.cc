#include "reporting.hpp"

#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <map>
#include <sstream>

using json = nlohmann::json;

std::string FormatEvalSummary(const std::vector<EvalReportedCase> &results) {
  int passed = 0;
  std::map<std::string, int> tag_total;
  std::map<std::string, int> tag_pass;
  std::map<std::string, int> metric_total;
  std::map<std::string, int> metric_pass;
  std::ostringstream failing;
  int fail_count = 0;

  for (const EvalReportedCase &row : results) {
    if (row.passed) {
      ++passed;
    } else {
      ++fail_count;
      if (fail_count <= 8) {
        failing << "  - " << row.id << ": " << row.reason << "\n";
      }
    }
    for (const std::string &tag : row.tags) {
      ++tag_total[tag];
      if (row.passed) {
        ++tag_pass[tag];
      }
    }
    for (const EvalMetricResult &metric : row.metrics) {
      ++metric_total[metric.name];
      if (metric.pass) {
        ++metric_pass[metric.name];
      }
    }
  }

  std::ostringstream out;
  out << "Eval results:\n";
  for (const EvalReportedCase &row : results) {
    out << "- " << row.id << ": " << (row.passed ? "PASS" : "FAIL") << " ("
        << row.reason << ")\n";
  }
  out << "Summary: " << passed << "/" << results.size() << " passed\n";
  out << "Pass rate by tag:\n";
  if (tag_total.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto &entry : tag_total) {
      out << "  " << entry.first << ": " << tag_pass[entry.first] << "/"
          << entry.second << "\n";
    }
  }
  out << "Pass rate by metric:\n";
  if (metric_total.empty()) {
    out << "  (none)\n";
  } else {
    for (const auto &entry : metric_total) {
      out << "  " << entry.first << ": " << metric_pass[entry.first] << "/"
          << entry.second << "\n";
    }
  }
  if (fail_count > 0) {
    out << "Top failing cases:\n" << failing.str();
  }
  return out.str();
}

absl::Status WriteEvalResultsJsonl(const std::string &path,
                                   const std::vector<EvalReportedCase> &results) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to write ", path));
  }
  for (const EvalReportedCase &row : results) {
    json metrics = json::array();
    for (const EvalMetricResult &metric : row.metrics) {
      metrics.push_back({{"name", metric.name},
                         {"pass", metric.pass},
                         {"reason", metric.reason}});
    }
    json line = {{"id", row.id},
                 {"passed", row.passed},
                 {"reason", row.reason},
                 {"tags", row.tags},
                 {"metrics", metrics}};
    out << line.dump() << '\n';
  }
  return absl::OkStatus();
}
