#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

docker build --platform linux/arm64 -t library-flutter-arm "${ROOT_DIR}/flutter_frontend_arm"
docker run --rm -it \
  -e LIBRARY_BACKEND_BIN="${LIBRARY_BACKEND_BIN:-/app/library_backend}" \
  -e LIBRARY_PG_CONN="${LIBRARY_PG_CONN:-host=localhost port=5432 dbname=library user=postgres password=123}" \
  -v "${ROOT_DIR}/flutter_frontend_windows:/app/frontend" \
  library-flutter-arm
