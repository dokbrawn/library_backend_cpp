#!/usr/bin/env bash
set -euo pipefail

BACKEND_PATH="${BACKEND_PATH:-/app/backend/library_backend}"
export LIBRARY_PG_CONN="${LIBRARY_PG_CONN:-host=localhost port=5432 dbname=library user=postgres password=123}"

if [[ ! -x "$BACKEND_PATH" ]]; then
  echo "[arm-run] Backend binary not found or not executable: $BACKEND_PATH"
  echo "[arm-run] Mount ARM build of backend to /app/backend or set BACKEND_PATH"
fi

exec java -jar /app/java-frontend.jar
