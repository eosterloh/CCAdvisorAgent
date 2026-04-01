# Testing

This project currently runs integration-style tests from `main.cc`.

## Scope (current)

- Gemini client tests:
  - generation request succeeds and returns non-empty content
  - embedding request succeeds and returns a non-empty vector
- Tool-calling tests:
  - scraper request succeeds in JSON mode and includes expected JSON fields

These tests target subsystems expected to change little while implementation is completed:

- `src/app/geminiclient/*`
- `src/app/toolcalling/*`

## Run

From the `build` directory:

```bash
bash chelp.bash
```

`main.cc` is currently the test entrypoint and runs all selected tests.

## Required environment variables

- `GEMINI_API_KEY`
- `JINA_AI_API_KEY`

If either is missing, relevant tests will fail with an error status.
