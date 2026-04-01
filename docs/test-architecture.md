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

- Move from `main.cc` orchestration to a dedicated test runner.
- Split integration tests from unit tests.
- Add stable fixtures and deterministic mocks for external API calls.
