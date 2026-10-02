# Test Architecture (Current)

## Entry point

- `main.cc` runs test suites directly for now.

## Test files

- `tests/gemini_client_tests.cc`
- `tests/tool_calling_tests.cc`

## Why this design right now

- Minimal setup overhead while core features are still under active development.
- Keeps important integration checks close to runtime behavior.
- Enables quick refactoring later into a dedicated runner/framework without rewriting test logic.

## Planned evolution

- Dedicated runners: `expansion_tests_runner`, `weaviate_tests_runner`, `evals_runner`.
- Deterministic scoring in `evals/scoring.cc` and reporting in `evals/reporting.cc`.
- Live Gemini/Jina still used for integration evals; unit scoring tests do not call external APIs.
