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
- expect_input_rejection: true (optional; skip helpfulness checks, require prompt text)

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
#include "app/plan/plan_artifacts.hpp"
#include "evalsuite.hpp"
#include "llm_judge.hpp"
#include "reporting.hpp"
#include "scoring.hpp"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  std::vector<std::string> followups;
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
    input.open("../evals/cases.jsonl");
  }
  if (!input.is_open()) {
    return tests;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.rfind("//", 0) == 0 || line.rfind("#", 0) == 0) {
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
    if (row.contains("queries") && row.at("queries").is_array()) {
      t.query.clear();
      t.followups.clear();
      for (const json::value_type &q : row.at("queries")) {
        if (!q.is_string()) {
          continue;
        }
        if (t.query.empty()) {
          t.query = q.get<std::string>();
        } else {
          t.followups.push_back(q.get<std::string>());
        }
      }
    }
    if (row.contains("followups") && row.at("followups").is_array()) {
      for (const json::value_type &q : row.at("followups")) {
        if (q.is_string()) {
          t.followups.push_back(q.get<std::string>());
        }
      }
    }
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
  for (const std::string &followup : test.followups) {
    script += followup;
    script += "\n";
  }
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

MetricCheck evaluateClarification(const std::string &advisor_text) {
  if (advisor_text.empty()) {
    return {false, "clarification failed: missing advisor response"};
  }
  const std::string lower = absl::AsciiStrToLower(advisor_text);
  const std::vector<std::string> clarify_terms = {
      "?", "clarif", "which", "could you", "tell me", "more about", "prefer"};
  if (!containsAny(lower, clarify_terms)) {
    return {false, "clarification failed: no question or narrowing ask"};
  }
  return {true, "clarification passed"};
}

EvalReportedCase runOneTest(const EvalTestCase &test) {
  std::cout << "[eval] Starting case: " << test.id << std::endl;
  EvalReportedCase row;
  row.id = test.id;
  row.tags = test.tags;
  row.passed = true;

  auto record = [&](const std::string &name, bool pass, const std::string &reason) {
    row.metrics.push_back({name, pass, reason});
    if (!pass) {
      row.passed = false;
      if (row.reason.empty()) {
        row.reason = reason;
      }
    }
  };

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
    record("chat", false, absl::StrCat("chat() failed: ", status.ToString()));
    std::cout << "[eval] Case failed early: " << test.id
              << " reason=chat() failed" << std::endl;
    return row;
  }
  record("chat", true, "chat() returned ok");

  const absl::StatusOr<bool> critical = NoFailedCriticalPhase(manager);
  record("critical_phases", critical.ok() && *critical,
         (critical.ok() && *critical) ? "no critical phase failures"
                                      : "One or more critical phases failed.");

  if (test.expected.contains("required_phases") &&
      test.expected.at("required_phases").is_array()) {
    for (const json::value_type &phase : test.expected.at("required_phases")) {
      if (!phase.is_string()) {
        continue;
      }
      const absl::StatusOr<bool> has_phase =
          HasPhase(manager, phase.get<std::string>());
      record(absl::StrCat("phase:", phase.get<std::string>()),
             has_phase.ok() && *has_phase,
             (has_phase.ok() && *has_phase)
                 ? "required phase present"
                 : absl::StrCat("Missing required phase: ",
                                phase.get<std::string>()));
    }
  }

  if (test.expected.contains("forbidden_phases") &&
      test.expected.at("forbidden_phases").is_array()) {
    for (const json::value_type &phase : test.expected.at("forbidden_phases")) {
      if (!phase.is_string()) {
        continue;
      }
      const absl::StatusOr<bool> has_phase =
          HasPhase(manager, phase.get<std::string>());
      const bool forbidden_present = has_phase.ok() && *has_phase;
      record(absl::StrCat("forbidden:", phase.get<std::string>()),
             !forbidden_present,
             forbidden_present
                 ? absl::StrCat("Forbidden phase was present: ",
                                phase.get<std::string>())
                 : "forbidden phase absent");
    }
  }

  if (test.expected.contains("min_successful_tools") &&
      test.expected.at("min_successful_tools").is_number_integer()) {
    const int min_tools = test.expected.at("min_successful_tools").get<int>();
    const bool enough = countSuccessfulTools(events) >= min_tools;
    record("min_successful_tools", enough,
           enough ? "enough successful tools"
                  : "Not enough successful tool calls.");
  }

  if (test.expected.contains("require_tool_name") &&
      test.expected.at("require_tool_name").is_string()) {
    const std::string required_tool =
        test.expected.at("require_tool_name").get<std::string>();
    const absl::StatusOr<bool> named =
        HasSuccessfulToolNamed(manager, required_tool);
    record(absl::StrCat("tool:", required_tool), named.ok() && *named,
           (named.ok() && *named)
               ? "required tool succeeded"
               : absl::StrCat("Required tool was not successfully called: ",
                              required_tool));
  }

  if (test.expected.contains("max_phase_latency_ms") &&
      test.expected.at("max_phase_latency_ms").is_object()) {
    const json &latency_limits = test.expected.at("max_phase_latency_ms");
    for (json::const_iterator it = latency_limits.begin();
         it != latency_limits.end(); ++it) {
      if (!it.value().is_number_integer()) {
        continue;
      }
      const absl::StatusOr<bool> under = PhaseLatencyUnder(
          manager, it.key(),
          std::chrono::milliseconds(it.value().get<int>()));
      record(absl::StrCat("latency:", it.key()), under.ok() && *under,
             (under.ok() && *under)
                 ? "latency within budget"
                 : absl::StrCat("Latency limit exceeded for phase: ", it.key()));
    }
  }

  if (test.expected.contains("must_finish_with_decider_done") &&
      test.expected.at("must_finish_with_decider_done").is_boolean() &&
      test.expected.at("must_finish_with_decider_done").get<bool>()) {
    const absl::StatusOr<bool> done = DeciderMarkedDone(manager);
    record("decider_done", done.ok() && *done,
           (done.ok() && *done) ? "decider marked done"
                                : "Decider did not report done=true.");
  }

  const bool expect_input_rejection =
      test.expected.contains("expect_input_rejection") &&
      test.expected.at("expect_input_rejection").is_boolean() &&
      test.expected.at("expect_input_rejection").get<bool>();
  if (expect_input_rejection) {
    const std::string tl = absl::AsciiStrToLower(transcript);
    const bool guided = absl::StrContains(tl, "please enter a question");
    record("input_rejection", guided,
           guided ? "empty input handled"
                  : "Expected prompt to reject empty input (no guidance text).");
    bool saw_input_trace = false;
    for (const TraceEvent &e : events) {
      if (e.phase == "input") {
        saw_input_trace = true;
        break;
      }
    }
    record("input_trace", saw_input_trace,
           saw_input_trace ? "input phase present"
                           : "Expected input-phase trace on empty query.");
    if (row.passed) {
      row.reason = "pass (empty input handled)";
      std::cout << "[eval] Case passed: " << test.id
                << " (input rejection)" << std::endl;
    } else {
      std::cout << "[eval] Case failed: " << test.id << " reason=" << row.reason
                << std::endl;
    }
    return row;
  }

  const absl::StatusOr<bool> responder = ResponderProducedOutput(manager);
  record("responder_output", responder.ok() && *responder,
         (responder.ok() && *responder) ? "responder produced output"
                                        : "responder did not produce output");

  const MetricCheck helpfulness = evaluateHelpfulness(advisor_text);
  record("helpfulness", helpfulness.pass, helpfulness.reason);

  const MetricCheck faithfulness = evaluateFaithfulness(events, test);
  record("faithfulness", faithfulness.pass, faithfulness.reason);

  const MetricCheck actionability = evaluateActionability(advisor_text);
  record("actionability", actionability.pass, actionability.reason);

  if (test.expected.contains("require_memory") &&
      test.expected.at("require_memory").is_boolean() &&
      test.expected.at("require_memory").get<bool>()) {
    const absl::StatusOr<bool> memory = MemoryUpdated(manager);
    record("memory", memory.ok() && *memory,
           (memory.ok() && *memory) ? "memory updated"
                                    : "memory phase missing");
  }

  if (test.expected.contains("require_clarification") &&
      test.expected.at("require_clarification").is_boolean() &&
      test.expected.at("require_clarification").get<bool>()) {
    const MetricCheck clarify = evaluateClarification(advisor_text);
    record("clarification", clarify.pass, clarify.reason);
  }

  if (test.expected.contains("require_substrings") &&
      test.expected.at("require_substrings").is_array()) {
    for (const json::value_type &needle : test.expected.at("require_substrings")) {
      if (!needle.is_string()) {
        continue;
      }
      const std::string needle_text = needle.get<std::string>();
      std::string haystack = transcript;
      const std::size_t qpos = haystack.find(test.query);
      if (qpos != std::string::npos) {
        haystack.erase(qpos, test.query.size());
      }
      const bool found = TranscriptContains(advisor_text, needle_text) ||
                         TranscriptContains(haystack, needle_text);
      record(absl::StrCat("substring:", needle_text), found,
             found ? "substring present"
                   : absl::StrCat("Missing required substring: ", needle_text));
    }
  }

  if (test.expected.contains("require_plan_file") &&
      test.expected.at("require_plan_file").is_boolean() &&
      test.expected.at("require_plan_file").get<bool>()) {
    const absl::StatusOr<bool> plan_ok = PlanFileHasRequiredSections("plan.md");
    record("plan_md", plan_ok.ok() && *plan_ok,
           (plan_ok.ok() && *plan_ok)
               ? "plan.md has required sections"
               : "plan.md missing or missing required sections");
  }

  if (test.expected.contains("require_plan_pdf") &&
      test.expected.at("require_plan_pdf").is_boolean() &&
      test.expected.at("require_plan_pdf").get<bool>()) {
    const bool pdf_ok = std::filesystem::exists("plan.pdf");
    record("plan_pdf", pdf_ok,
           pdf_ok ? "plan.pdf written" : "plan.pdf missing");
  }

  if (test.expected.contains("llm_judge") &&
      test.expected.at("llm_judge").is_boolean() &&
      test.expected.at("llm_judge").get<bool>()) {
    std::string evidence;
    for (const TraceEvent &event : events) {
      if (event.evidence.has_value() && !event.evidence->empty()) {
        evidence = *event.evidence;
      }
    }
    const EvalMetricResult judged =
        RunLlmJudge(test.query, advisor_text, evidence);
    record(judged.name, judged.pass, judged.reason);
  }

  if (row.passed) {
    row.reason = "pass";
    std::cout << "[eval] Case passed: " << test.id << std::endl;
  } else {
    std::cout << "[eval] Case failed: " << test.id << " reason=" << row.reason
              << std::endl;
  }
  return row;
}

} // namespace

EvalSuiteOutcome RunEvalSuite() {
  std::vector<EvalTestCase> testcases = getTests("evals/cases.jsonl");
  const std::vector<EvalTestCase> agent_cases =
      getTests("evals/cases/agentcases.jsonl");
  testcases.insert(testcases.end(), agent_cases.begin(), agent_cases.end());
  EvalSuiteOutcome outcome;
  if (testcases.empty()) {
    outcome.summary = "No eval cases loaded from evals/cases.jsonl";
    return outcome;
  }

  std::cout << "[eval] Loaded " << testcases.size() << " test cases."
            << std::endl;
  std::vector<EvalReportedCase> reported;
  for (const EvalTestCase &test : testcases) {
    const EvalReportedCase row = runOneTest(test);
    reported.push_back(row);
    ++outcome.total;
    if (row.passed) {
      ++outcome.passed;
    }
  }
  const absl::Status write_status =
      WriteEvalResultsJsonl("evals/eval_results.jsonl", reported);
  if (!write_status.ok()) {
    std::cerr << write_status << std::endl;
  }
  outcome.summary = FormatEvalSummary(reported);
  return outcome;
}

std::string evalsuite() { return RunEvalSuite().summary; }