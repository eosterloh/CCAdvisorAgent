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

int evalsuite() {
  // First: lets do some RAG tests
  // Make sure coverage of tests is really good
  // Run a check on the results for a lot of different
  // parameters.
  // Run this whenever a vector is retrieved in the
  // Agent as well.
  checkWeaviateResults(query, results);
  // Next Evals for all the agents
  // Before this: read an article on Agent
  // Evals
  // Should run thru
}