# CCAdvisorAgent

`CCAdvisorAgent` is a C++ advising assistant for Colorado College planning workflows.  
It combines structured profile intake, retrieval over catalog content, and LLM reasoning to
produce grounded advising responses.

## Core Capabilities

- Collects student context through a preliminary onboarding flow.
- Ingests major-specific catalog data into Weaviate with embeddings.
- Runs major-aware retrieval with readiness/fallback signaling while ingestion is in progress.
- Supports tool-based operations (`scrape`, `retrieve_from_weaviate`) plus direct responder flow.
- Generates memory-updated advising conversations and a final markdown plan artifact.

## Architecture Overview

- **LLM clients**: Gemini generation and embedding interfaces.
- **Storage**: Weaviate `CourseChunk` class for embedded chunks.
- **Metadata**: stored per chunk (`major_key`, `major_name`, `course_code`, source fields).
- **Ingestion modes**:
  - Legacy batch ingestion from local JSONL (`send_data_to_weaviate`).
  - Major-agnostic runtime ingestion (`major_ingest_runner`) launched from onboarding.
- **Chat orchestration**: planner -> orchestrator/tools -> observer/responder -> memory -> decider.

## Requirements

- macOS or Linux
- CMake 3.14+
- C++17 toolchain
- Docker (for local Weaviate)
- API keys:
  - `GEMINI_API_KEY`
  - `JINA_AI_API_KEY`

Create a local `.env` in repo root:

```bash
GEMINI_API_KEY=your_key_here
JINA_AI_API_KEY=your_key_here
```

Load env vars in your current shell before running binaries:

```bash
set -a
source .env
set +a
```

## Build

From repository root:

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

Primary targets:

- `AdvisorAgBuild` - main executable (tests + chat sandbox entrypoint)
- `major_ingest_runner` - major-agnostic scrape/embed/populate pipeline
- `send_data_to_weaviate` - batch ingest from local JSONL files
- `scraper_script` - scraper utility binary
- `weaviate_tests_runner` - focused Weaviate integration tests

## Local Weaviate

Start Weaviate:

```bash
cd docker
docker compose up -d
cd ..
```

Configure schema:

```bash
bash scripts/config_weaviate.sh
```

`config_weaviate.sh` is idempotent and now handles both class creation and required property
upgrade checks.

## Major-Agnostic Runtime Ingestion

The onboarding flow launches background ingestion immediately after major capture.

Manual run:

```bash
bash scripts/populate_major_in_background.sh "Computer Science"
```

Status file:

```text
data/major_ingest/<major_key>.status.json
```

Possible states:

- `starting`
- `running`
- `ready`
- `failed`

The retrieval path is status-aware:

- If status is `ready`, retrieval uses major-filtered Weaviate queries.
- If status is not ready, it returns structured fallback context so chat can continue safely.

## Legacy Batch Data Pipeline (Optional)

If you want to pre-populate from local JSONL scrape outputs:

```bash
bash scripts/populate_data_file.sh
./build/send_data_to_weaviate
```

## Running the App

```bash
./build/AdvisorAgBuild
```

At launch, the app enters onboarding/chat directly.

## Verification and Testing

### 1) Build verification

```bash
cmake --build build --target AdvisorAgBuild major_ingest_runner send_data_to_weaviate -j
```

### 2) Weaviate object count sanity check

```bash
curl -sS "http://localhost:8080/v1/graphql" \
  -H "Content-Type: application/json" \
  -d '{"query":"{ Aggregate { CourseChunk { meta { count } } } }"}'
```

### 3) Weaviate-focused tests

```bash
./build/weaviate_tests_runner
```

### 4) Tooling tests

`AdvisorAgBuild` runs integrated tests before chat starts.
Use dedicated test binaries and targets (for example `weaviate_tests_runner`) for
focused validation.

## Repository Structure

```text
CCAdvisorAgent/
├── include/app/                 # public headers
├── src/app/                     # implementation
├── scripts/                     # ingestion/schema helper scripts + tool binaries
├── data/                        # scraped and ingestion status artifacts
├── docker/                      # local Weaviate runtime
├── tests/                       # integration-style tests
├── docs/
├── CMakeLists.txt
└── main.cc
```

## Troubleshooting

- **Gemini 403 / PERMISSION_DENIED**
  - Ensure `.env` is loaded in your current shell (`set -a; source .env; set +a`).
  - Verify key presence: `echo "$GEMINI_API_KEY" | wc -c`.

- **Binary not found**
  - Correct main executable name is `AdvisorAgBuild`.
  - From repo root: `./build/AdvisorAgBuild`.
  - From `build/`: `./AdvisorAgBuild`.

- **Ingestion status stays `failed`**
  - Confirm Docker Weaviate is up.
  - Run `bash scripts/config_weaviate.sh` manually and inspect output.
  - Verify both API keys are loaded.

## Notes

- This project is actively evolving; interfaces and prompts may change as retrieval grounding
  and evaluation coverage expand.