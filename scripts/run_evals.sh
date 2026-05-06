#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "[evals] Starting eval pipeline..."
echo "[evals] Project root: ${PROJECT_ROOT}"

if [ -f "${PROJECT_ROOT}/.env" ]; then
  echo "[evals] Loading environment from .env"
  set -a
  # shellcheck disable=SC1091
  source "${PROJECT_ROOT}/.env"
  set +a
else
  echo "[evals] No .env file found; using existing shell environment."
fi

echo "[evals] Configuring CMake..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
echo "[evals] Building evals_runner target..."
cmake --build "${BUILD_DIR}" --target evals_runner -j

echo "[evals] Running evals_runner..."
"${BUILD_DIR}/evals_runner"
echo "[evals] Eval pipeline complete."
