#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: populate_major_in_background.sh <major name>"
  exit 1
fi

major_name="$1"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
build_dir="${project_root}/build"

major_key="$(printf "%s" "${major_name}" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/_/g; s/^_+//; s/_+$//')"
if [ -z "${major_key}" ]; then
  major_key="unspecified_major"
fi

status_dir="${project_root}/data/major_ingest"
mkdir -p "${status_dir}"
status_file="${status_dir}/${major_key}.status.json"

write_status() {
  local state="$1"
  local message="$2"
  python3 - "${status_file}" "${major_key}" "${major_name}" "${state}" "${message}" <<'PY'
import json
import pathlib
import sys

status_path = pathlib.Path(sys.argv[1])
payload = {
    "major_key": sys.argv[2],
    "major_name": sys.argv[3],
    "state": sys.argv[4],
    "message": sys.argv[5],
}
status_path.write_text(json.dumps(payload), encoding="utf-8")
PY
}

write_status "starting" "Preparing major ingestion."

if ! bash "${script_dir}/config_weaviate.sh"; then
  write_status "failed" "Weaviate schema configuration failed."
  exit 1
fi

if ! cmake -S "${project_root}" -B "${build_dir}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
  write_status "failed" "CMake configure failed."
  exit 1
fi

if ! cmake --build "${build_dir}" --target major_ingest_runner; then
  write_status "failed" "Build of major_ingest_runner failed."
  exit 1
fi

write_status "running" "Ingestion is in progress."
if ! "${build_dir}/major_ingest_runner" "${major_name}" "${major_key}"; then
  write_status "failed" "major_ingest_runner failed. See logs."
  exit 1
fi

write_status "ready" "Major ingestion complete."
