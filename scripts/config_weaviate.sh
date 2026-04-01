#!/usr/bin/env bash
set -euo pipefail

WEAVIATE_URL="${WEAVIATE_URL:-http://localhost:8080}"

status_code="$(curl -s -o /dev/null -w "%{http_code}" \
  "${WEAVIATE_URL}/v1/schema/CourseChunk")"

if [ "${status_code}" = "200" ]; then
  echo "CourseChunk schema already exists. Skipping creation."
  exit 0
fi

curl -sS -X POST "${WEAVIATE_URL}/v1/schema" \
  -H "Content-Type: application/json" \
  -d '{
    "class": "CourseChunk",
    "description": "Chunked course catalog content for advising retrieval",
    "vectorizer": "none",
    "vectorIndexType": "hnsw",
    "vectorIndexConfig": {
      "distance": "cosine"
    },
    "properties": [
      { "name": "course_code", "dataType": ["text"] },
      { "name": "title", "dataType": ["text"] },
      { "name": "source_path", "dataType": ["text"] },
      { "name": "source_url", "dataType": ["text"] },
      { "name": "chunk_text", "dataType": ["text"] }
    ]
  }'

echo "Created CourseChunk schema."