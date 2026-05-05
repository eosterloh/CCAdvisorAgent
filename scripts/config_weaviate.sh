#!/usr/bin/env bash
set -euo pipefail

WEAVIATE_URL="${WEAVIATE_URL:-http://localhost:8080}"

status_code="$(curl -s -o /dev/null -w "%{http_code}" \
  "${WEAVIATE_URL}/v1/schema/CourseChunk")"

ensure_property() {
  local prop_name="$1"
  local payload
  payload="$(cat <<EOF
{"name":"${prop_name}","dataType":["text"]}
EOF
)"
  local tmp_body
  tmp_body="$(mktemp)"
  local prop_status
  prop_status="$(
    curl -s -o "${tmp_body}" -w "%{http_code}" \
      -X POST "${WEAVIATE_URL}/v1/schema/CourseChunk/properties" \
      -H "Content-Type: application/json" \
      -d "${payload}"
  )"
  if [ "${prop_status}" = "200" ] || [ "${prop_status}" = "201" ]; then
    echo "Added CourseChunk property: ${prop_name}"
  elif [ "${prop_status}" = "409" ] || [ "${prop_status}" = "422" ]; then
    echo "CourseChunk property already present: ${prop_name}"
  else
    echo "Failed to add CourseChunk property '${prop_name}' (HTTP ${prop_status})."
    cat "${tmp_body}"
    rm -f "${tmp_body}"
    exit 1
  fi
  rm -f "${tmp_body}"
}

if [ "${status_code}" = "404" ]; then
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
        { "name": "major_key", "dataType": ["text"] },
        { "name": "major_name", "dataType": ["text"] },
        { "name": "course_code", "dataType": ["text"] },
        { "name": "title", "dataType": ["text"] },
        { "name": "source_path", "dataType": ["text"] },
        { "name": "source_url", "dataType": ["text"] },
        { "name": "chunk_text", "dataType": ["text"] }
      ]
    }'

  echo "Created CourseChunk schema."
elif [ "${status_code}" = "200" ]; then
  echo "CourseChunk schema already exists. Checking required properties..."
else
  echo "Failed to inspect CourseChunk schema (HTTP ${status_code})."
  exit 1
fi

ensure_property "major_key"
ensure_property "major_name"
ensure_property "course_code"
ensure_property "title"
ensure_property "source_path"
ensure_property "source_url"
ensure_property "chunk_text"