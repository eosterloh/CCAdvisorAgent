/*
Eval Suite Plan (C++-first, trace-driven)
=========================================

1) Eval Suite Structure
-----------------------
- evals/cases.jsonl
  - One test case per line.
  - Fields: id, mode ("test"/"chat"), query, expected (rules), optional tags.

- evals/evalsuite.cc
  - Load cases.
  - Run each case through chat_manager (prefer "test" preset mode).
  - Collect traces via getTraceEvents().
  - Score with deterministic checks.
  - Write eval_results.jsonl and print summary to stdout.

- evals/scoring.hpp / evals/scoring.cc
  - Pure scoring functions:
    - HasPhase(...)
    - PhaseLatencyUnder(...)
    - HasSuccessfulTool(...)
    - NoFailedCriticalPhase(...)
    - ResponderProducedOutput(...)

- evals/reporting.hpp / evals/reporting.cc
  - Aggregate pass rate by metric and by tag.
  - Print top failing cases and reasons.

2) Case Schema (Practical)
--------------------------
Expected/rule fields to support in each case:
- required_phases: ["planner", "decider", "memory"]
- forbidden_phases: []
- min_successful_tools: 0 (or 1 for tool-required queries)
- max_phase_latency_ms: { "planner": 8000, "observer": 8000 }
- must_finish_with_decider_done: false (often false for single-turn smoke)
- require_tool_name: "retrieve_from_weaviate" (optional)

3) Sample Run Query Flow (One Case)
-----------------------------------
Case:
- id: "cs-next-block-1"
- mode: "test"
- query: "I want help planning my next semester in CS with AI focus."
- expected:
  - required phases: planner, decider
  - at least 1 successful tool call OR successful responder
  - no failed planner/orchestrator/observer
  - planner latency < 10s

Execution:
1. Instantiate chat_manager.
2. loadTestProfilePreset().
3. Feed input stream with:
   - query line
   - optional follow-up line / EOF based on chat loop behavior.
4. Run chat(i, o).
5. Read traces:
   - const std::vector<TraceEvent>& traces = getTraceEvents();
6. Score:
   - planner ran and succeeded
   - either tool path succeeded or responder succeeded
   - no critical phase failures
   - latency thresholds pass
7. Emit case result with per-metric breakdown.

4) Scoring Tiers
----------------
Tier 1 (must pass):
- No critical failures (planner, orchestrator, observer, decider)
- At least one valid response path completed

Tier 2 (quality):
- Tool correctness (if tool expected)
- Memory updated
- Latency SLOs

Tier 3 (stretch):
- Grounding checks (evidence/tool usage before factual recommendations)

5) First 10 Eval Cases To Start
-------------------------------
- 3 direct-response queries (no tools expected)
- 3 retrieval-heavy advising queries (tool expected)
- 2 ambiguous queries (should clarify / avoid overconfidence)
- 1 malformed or empty input case
- 1 stress case (long query + latency assertions)
*/

#include "../include/app/common/types.hpp"
#include "app/conversation/chat_management.hpp"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct EvalTestCase {
  std::string id;
  std::string mode;
  std::string query;
  json expected;
  std::vector<std::string> tags;
};

struct EvalCaseResult {
  std::string id;
  bool passed;
  std::string reason;
};

struct MetricCheck {
  bool pass;
  std::string reason;
};

std::vector<EvalTestCase>
getTests(const std::string &path = "evals/cases.jsonl") {
  std::vector<EvalTestCase> tests;
  std::ifstream input(path);
  if (!input.is_open()) {
    return tests;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const json row = json::parse(line, nullptr, false);
    if (row.is_discarded()) {
      continue;
    }

    EvalTestCase t;
    t.id = row.value("id", "");
    t.mode = row.value("mode", "test");
    t.query = row.value("query", "");
    t.expected = row.value("expected", json::object());
    if (row.contains("tags") && row.at("tags").is_array()) {
      for (const json::value_type &tag : row.at("tags")) {
        if (tag.is_string()) {
          t.tags.push_back(tag.get<std::string>());
        }
      }
    }
    tests.push_back(t);
  }
  return tests;
}

std::string buildInputScript(const EvalTestCase &test) {
  std::string script = test.query;
  script += "\n";
  return script;
}

bool containsPhase(const std::vector<TraceEvent> &events,
                   const std::string &phase) {
  for (const TraceEvent &e : events) {
    if (e.phase == phase) {
      return true;
    }
  }
  return false;
}

int countSuccessfulTools(const std::vector<TraceEvent> &events) {
  int count = 0;
  for (const TraceEvent &e : events) {
    if (e.phase == "tool" && e.success) {
      ++count;
    }
  }
  return count;
}

bool hasSuccessfulToolName(const std::vector<TraceEvent> &events,
                           const std::string &tool_name) {
  for (const TraceEvent &e : events) {
    if (e.phase == "tool" && e.success && e.tool_name.has_value() &&
        *e.tool_name == tool_name) {
      return true;
    }
  }
  return false;
}

bool allCriticalPhasesSucceeded(const std::vector<TraceEvent> &events) {
  for (const TraceEvent &e : events) {
    if ((e.phase == "planner" || e.phase == "orchestrator" ||
         e.phase == "observer" || e.phase == "decider") &&
        !e.success) {
      return false;
    }
  }
  return true;
}

bool phaseLatencyUnder(const std::vector<TraceEvent> &events,
                       const std::string &phase, int max_ms) {
  for (const TraceEvent &e : events) {
    if (e.phase == phase && e.latency.count() > max_ms) {
      return false;
    }
  }
  return true;
}

bool deciderDoneObserved(const std::vector<TraceEvent> &events) {
  for (const TraceEvent &e : events) {
    if (e.phase == "decider" &&
        e.event_summary == "Decider marked workflow complete.") {
      return true;
    }
  }
  return false;
}

bool containsAny(const std::string &haystack,
                 const std::vector<std::string> &needles) {
  for (const std::string &needle : needles) {
    if (absl::StrContains(haystack, needle)) {
      return true;
    }
  }
  return false;
}

std::string extractAdvisorText(const std::string &transcript) {
  const std::string marker = "Advisor: ";
  const std::size_t pos = transcript.rfind(marker);
  if (pos == std::string::npos) {
    return "";
  }
  const std::size_t start = pos + marker.size();
  const std::size_t end = transcript.find('\n', start);
  if (end == std::string::npos) {
    return transcript.substr(start);
  }
  return transcript.substr(start, end - start);
}

MetricCheck evaluateHelpfulness(const std::string &advisor_text) {
  if (advisor_text.empty()) {
    return {false, "helpfulness failed: missing advisor response text"};
  }
  if (advisor_text.size() < 60) {
    return {false, "helpfulness failed: response too short"};
  }
  const std::string lower = absl::AsciiStrToLower(advisor_text);
  const std::vector<std::string> helpful_terms = {
      "recommend", "suggest", "should", "consider", "plan", "option"};
  if (!containsAny(lower, helpful_terms)) {
    return {false, "helpfulness failed: no recommendation language detected"};
  }
  return {true, "helpfulness passed"};
}

MetricCheck evaluateFaithfulness(const std::vector<TraceEvent> &events,
                                 const EvalTestCase &test) {
  bool grounding_expected = false;
  if (test.expected.contains("require_tool_name") &&
      test.expected.at("require_tool_name").is_string()) {
    const std::string required_tool =
        test.expected.at("require_tool_name").get<std::string>();
    if (required_tool == "retrieve_from_weaviate") {
      grounding_expected = true;
    }
  }
  for (const std::string &tag : test.tags) {
    if (tag == "retrieval" || tag == "grounding") {
      grounding_expected = true;
    }
  }

  if (!grounding_expected) {
    return {true, "faithfulness passed (no grounding requirement for case)"};
  }

  if (!hasSuccessfulToolName(events, "retrieve_from_weaviate")) {
    return {false,
            "faithfulness failed: retrieval grounding tool was not successful"};
  }
  return {true, "faithfulness passed"};
}

MetricCheck evaluateActionability(const std::string &advisor_text) {
  if (advisor_text.empty()) {
    return {false, "actionability failed: missing advisor response text"};
  }
  const std::string lower = absl::AsciiStrToLower(advisor_text);
  const std::vector<std::string> action_terms = {
      "next", "take", "enroll", "plan", "step", "option", "consider"};
  if (!containsAny(lower, action_terms)) {
    return {false, "actionability failed: no actionable language detected"};
  }

  int sentence_markers = 0;
  for (const char ch : advisor_text) {
    if (ch == '.' || ch == '!' || ch == '?') {
      ++sentence_markers;
    }
  }
  if (sentence_markers < 1) {
    return {false, "actionability failed: response is not sentence-like"};
  }
  return {true, "actionability passed"};
}

EvalCaseResult runOneTest(const EvalTestCase &test) {
  chat_manager manager;
  if (test.mode == "test") {
    manager.loadTestProfilePreset();
  }

  const std::string script = buildInputScript(test);
  std::istringstream fake_input(script);
  std::ostringstream fake_output;
  const absl::Status status = manager.chat(fake_input, fake_output);
  const std::vector<TraceEvent> &events = manager.getTraceEvents();
  const std::string transcript = fake_output.str();
  const std::string advisor_text = extractAdvisorText(transcript);

  if (!status.ok()) {
    return {test.id, false, absl::StrCat("chat() failed: ", status)};
  }
  if (!allCriticalPhasesSucceeded(events)) {
    return {test.id, false, "One or more critical phases failed."};
  }

  if (test.expected.contains("required_phases") &&
      test.expected.at("required_phases").is_array()) {
    for (const json::value_type &phase : test.expected.at("required_phases")) {
      if (!phase.is_string()) {
        continue;
      }
      if (!containsPhase(events, phase.get<std::string>())) {
        return {
            test.id, false,
            absl::StrCat("Missing required phase: ", phase.get<std::string>())};
      }
    }
  }

  if (test.expected.contains("min_successful_tools") &&
      test.expected.at("min_successful_tools").is_number_integer()) {
    const int min_tools = test.expected.at("min_successful_tools").get<int>();
    if (countSuccessfulTools(events) < min_tools) {
      return {test.id, false, "Not enough successful tool calls."};
    }
  }

  if (test.expected.contains("require_tool_name") &&
      test.expected.at("require_tool_name").is_string()) {
    const std::string required_tool =
        test.expected.at("require_tool_name").get<std::string>();
    if (!hasSuccessfulToolName(events, required_tool)) {
      return {test.id, false,
              absl::StrCat("Required tool was not successfully called: ",
                           required_tool)};
    }
  }

  if (test.expected.contains("max_phase_latency_ms") &&
      test.expected.at("max_phase_latency_ms").is_object()) {
    const json &latency_limits = test.expected.at("max_phase_latency_ms");
    for (json::const_iterator it = latency_limits.begin();
         it != latency_limits.end(); ++it) {
      if (!it.value().is_number_integer()) {
        continue;
      }
      if (!phaseLatencyUnder(events, it.key(), it.value().get<int>())) {
        return {test.id, false,
                absl::StrCat("Latency limit exceeded for phase: ", it.key())};
      }
    }
  }

  if (test.expected.contains("must_finish_with_decider_done") &&
      test.expected.at("must_finish_with_decider_done").is_boolean() &&
      test.expected.at("must_finish_with_decider_done").get<bool>() &&
      !deciderDoneObserved(events)) {
    return {test.id, false, "Decider did not report done=true."};
  }

  const MetricCheck helpfulness = evaluateHelpfulness(advisor_text);
  if (!helpfulness.pass) {
    return {test.id, false, helpfulness.reason};
  }

  const MetricCheck faithfulness = evaluateFaithfulness(events, test);
  if (!faithfulness.pass) {
    return {test.id, false, faithfulness.reason};
  }

  const MetricCheck actionability = evaluateActionability(advisor_text);
  if (!actionability.pass) {
    return {test.id, false, actionability.reason};
  }

  return {test.id, true, "pass"};
}

} // namespace

std::string evalsuite() {
  const std::vector<EvalTestCase> testcases = getTests();
  if (testcases.empty()) {
    return "No eval cases loaded from evals/cases.jsonl";
  }

  int tests_passed = 0;
  std::ostringstream report;
  report << "Eval results:\n";
  for (const EvalTestCase &test : testcases) {
    const EvalCaseResult result = runOneTest(test);
    report << "- " << result.id << ": " << (result.passed ? "PASS" : "FAIL")
           << " (" << result.reason << ")\n";
    if (result.passed) {
      ++tests_passed;
    }
  }
  report << "Summary: " << tests_passed << "/" << testcases.size() << " passed";
  return report.str();
}