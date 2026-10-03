# Testing

## Unit and subsystem runners

- `expansion_tests_runner` — COI email, advisor escalation, plan artifacts, scoring helpers (no live APIs required).
- `weaviate_tests_runner` — Weaviate embed + nearVector retrieve.
- `AdvisorAgBuild` — Gemini, tool-calling, and Weaviate integration tests, then interactive chat.

## Eval suite

From repository root:

```bash
bash scripts/run_evals.sh
```

Writes `evals/eval_results.jsonl` with per-metric rows and prints pass rates by tag and metric.
Loads `evals/cases.jsonl` plus `evals/cases/agentcases.jsonl`.
`evals_runner` exits non-zero if any case fails.

Quality checks now include:

- deterministic phase/tool/latency/memory/plan-artifact/substring metrics
- optional LLM-as-judge (`expected.llm_judge`)
- multi-turn `followups` scripts

## Required environment variables

- `GEMINI_API_KEY`
- `JINA_AI_API_KEY`

Seed Computer Science catalog chunks before retrieval-heavy evals:

```bash
cmake --build build --target seed_cs_catalog -j
./build/seed_cs_catalog
```
