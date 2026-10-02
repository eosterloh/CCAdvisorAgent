#include "app/plan/plan_artifacts.hpp"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

bool HasRequiredPlanSections(std::string_view markdown) {
  for (const std::string &section : RequiredAcademicPlanSections()) {
    if (!absl::StrContains(markdown, section)) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<std::string> EnsureAcademicPlanMarkdown(std::string markdown) {
  if (HasRequiredPlanSections(markdown)) {
    return markdown;
  }
  return std::string(
      "# Academic Plan\n\n"
      "## Student Profile\n"
      "- Profile details were incomplete in the generated draft.\n\n"
      "## Recommended Course Path\n"
      "- Review retrieved catalog evidence and confirm next courses with your "
      "advisor.\n\n"
      "## Risks and Open Questions\n"
      "- Prerequisite fit and workload still need confirmation.\n\n"
      "## Next Actions\n"
      "- Verify course numbers and meet with your advisor before enrollment.\n");
}

absl::Status WriteAcademicPlanMarkdown(std::string_view markdown,
                                       std::string_view path) {
  std::ofstream out{std::string(path)};
  if (!out.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open ", path, " for writing."));
  }
  std::istringstream in{std::string(markdown)};
  std::string line;
  while (std::getline(in, line)) {
    out << line << '\n';
  }
  return absl::OkStatus();
}

namespace {
std::string EscapePdfText(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (c == '\\' || c == '(' || c == ')') {
      out.push_back('\\');
    }
    if (c == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

std::vector<std::string> WrapLines(std::string_view text, std::size_t width) {
  std::vector<std::string> lines;
  std::istringstream in{std::string(text)};
  std::string raw;
  while (std::getline(in, raw)) {
    if (raw.empty()) {
      lines.emplace_back("");
      continue;
    }
    std::size_t start = 0;
    while (start < raw.size()) {
      if (raw.size() - start <= width) {
        lines.push_back(raw.substr(start));
        break;
      }
      std::size_t split = start + width;
      const std::size_t space = raw.rfind(' ', split);
      if (space != std::string::npos && space > start) {
        split = space;
      }
      lines.push_back(raw.substr(start, split - start));
      start = (split < raw.size() && raw[split] == ' ') ? split + 1 : split;
    }
  }
  return lines;
}
} // namespace

absl::Status WriteSimplePdf(std::string_view text, std::string_view path) {
  const std::vector<std::string> lines = WrapLines(text, 92);
  std::ostringstream content;
  content << "BT\n/F1 11 Tf\n14 TL\n50 780 Td\n";
  for (const std::string &line : lines) {
    content << "(" << EscapePdfText(line) << ") Tj\nT*\n";
  }
  content << "ET\n";
  const std::string stream = content.str();

  std::ostringstream pdf;
  pdf << "%PDF-1.4\n";
  const std::vector<std::string> objects = {
      "1 0 obj << /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
      "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
      "/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n",
      absl::StrCat("4 0 obj << /Length ", stream.size(),
                   " >>\nstream\n", stream, "endstream\nendobj\n"),
      "5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"};

  std::vector<long> offsets;
  offsets.push_back(0);
  for (const std::string &obj : objects) {
    offsets.push_back(static_cast<long>(pdf.str().size()));
    pdf << obj;
  }
  const long xref_pos = static_cast<long>(pdf.str().size());
  pdf << "xref\n0 " << (objects.size() + 1) << "\n";
  pdf << "0000000000 65535 f \n";
  for (std::size_t i = 1; i < offsets.size(); ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%010ld 00000 n \n", offsets[i]);
    pdf << buf;
  }
  pdf << "trailer << /Size " << (objects.size() + 1)
      << " /Root 1 0 R >>\nstartxref\n"
      << xref_pos << "\n%%EOF\n";

  std::ofstream out{std::string(path), std::ios::binary};
  if (!out.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open ", path, " for writing."));
  }
  const std::string payload = pdf.str();
  out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  return absl::OkStatus();
}
