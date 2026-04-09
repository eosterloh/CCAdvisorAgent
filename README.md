# CCAdvisorAgent

LLM-powered advising assistant for Colorado College students.

The project combines:
- scraping CC academic pages,
- generating embeddings with Gemini,
- storing and retrieving chunks in Weaviate,
- and an emerging chat loop for advising-style interactions.

## Current Status

Implemented today:
- C++17 project with CMake + FetchContent dependencies (`absl`, `cpr`, `nlohmann/json`)
- scraping utilities and data pipeline scripts
- Gemini generation and embedding clients
- Weaviate REST/GraphQL integration (insert + `nearVector` retrieval)
- ingestion executable to populate Weaviate from `data/rawscrape/*.jsonl`
- integration-style test runners for Gemini, tool-calling, and Weaviate paths

Planned next:
- richer chat orchestration and tool routing
- clearer prompt/grounding pipeline
- evaluation harness for advising quality and safety
- more robust retrieval/reranking strategy and prerequisite-aware reasoning

## Repository Layout

```text
CCAdvisorAgent/
├── include/app/
│   ├── common/
│   ├── conversation/
│   ├── geminiclient/
│   ├── toolcalling/
│   └── weaviate/
├── src/app/
│   ├── conversation/
│   ├── geminiclient/
│   ├── toolcalling/
│   └── weaviate/
├── scripts/
│   ├── populate_data_file.sh
│   ├── config_weaviate.sh
│   ├── scraperscript.cc
│   └── send_data_to_weaviate.cc
├── data/rawscrape/
├── docker/
│   └── docker-compose.yml
├── tests/
├── docs/
├── CMakeLists.txt
└── main.cc
```

## Prerequisites

- macOS/Linux with C++17 toolchain
- CMake 3.14+
- Docker (for Weaviate)
- API keys:
  - `GEMINI_API_KEY`
  - `JINA_AI_API_KEY`

Create a local `.env` (ignored by git):

```bash
GEMINI_API_KEY=your_key_here
JINA_AI_API_KEY=your_key_here
```

## Build

From repo root:

```bash
cmake -S . -B build
cmake --build build
```

Primary targets:
- `AdvisorAgBuild` (main app / integrated runner)
- `scraper_script`
- `weaviate_tests_runner`
- `send_data_to_weaviate`

## Local Weaviate Setup

Start Weaviate:

```bash
cd docker
docker compose up -d
cd ..
```

Create schema (idempotent):

```bash
bash scripts/config_weaviate.sh
```

## Data Pipeline

1. Scrape into JSONL:

```bash
bash scripts/populate_data_file.sh
```

2. Embed + ingest into Weaviate:

```bash
set -a && source .env && set +a
./build/send_data_to_weaviate
```

3. Quick sanity check object count:

```bash
curl -sS "http://localhost:8080/v1/graphql" \
  -H "Content-Type: application/json" \
  -d '{"query":"{ Aggregate { CourseChunk { meta { count } } } }"}'
```

## Tests

Run Weaviate-only tests:

```bash
./build/weaviate_tests_runner
```

Run integrated tests from main target:

```bash
./build/AdvisorAgBuild
```

Notes:
- Some tests depend on live services and environment variables.
- Gemini-dependent paths require `GEMINI_API_KEY`.

## Design Notes

- Weaviate class currently uses single-vector storage with manual embeddings.
- Ingestion uses deterministic object IDs to reduce duplicate inserts.
- Retrieval path performs query embedding first, then GraphQL `nearVector`.

## Roadmap (Short Horizon)

- Harden chat manager loop (`conversation/`) for real user sessions
- Introduce tool gating policy (when to scrape vs retrieve vs answer)
- Add retrieval filtering/reranking and confidence thresholds
- Build eval sets for accuracy, citation quality, and safe escalation
- Add prerequisite extraction enrichment from linked course pages


Set up:

copy .env-example into .env and get API keys for all listed values

run these commands:

``` bash
set -a 
source .env
set +a

Cmake stuff: 

bash scripts/populate_data_file.sh
bash scripts/configure_weaviate.sh

cd docker
docker compose up -d
cd ..



```