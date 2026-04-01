#!/usr/bin/env bash
# Script to populate raw scraped data files.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
SCRAPER_EXE="${BUILD_DIR}/scraper_script"

#will have to expand to all majors: otherwise know as 
# a lot of data
#DO NOT USE AGAIN, IF RUNNING THIS SCRIPT CHANGE THESE FILES
declare -a URLSTOPARSE=(
  "https://coursecatalog.coloradocollege.edu/programs/BESO-BA-Major"
  "https://coursecatalog.coloradocollege.edu/departments/ECBU/courses"
  "https://coursecatalog.coloradocollege.edu/programs/COSC-BA-Major"
  "https://coursecatalog.coloradocollege.edu/departments/MATH/courses"
)

declare -a DESTFILE=(
  "besoc_reqs.jsonl"
  "besoc_courses.jsonl"
  "compsci_reqs.jsonl"
  "compsci_courses.jsonl"
)


mkdir -p "${PROJECT_ROOT}/data/rawscrape"

# Configure and compile scraper_script.
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "${BUILD_DIR}" --target scraper_script

length=${#URLSTOPARSE[@]}
for ((i = 0; i < length; i++)); do
  "${SCRAPER_EXE}" "${URLSTOPARSE[$i]}" "${PROJECT_ROOT}/data/rawscrape/${DESTFILE[$i]}"
  echo "populated ${DESTFILE[$i]} with url: ${URLSTOPARSE[$i]}"
done