#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHON="$ROOT/.venv/bin/python"
if [[ ! -x "$PYTHON" ]]; then
  echo "Python environment not found. Run: bash scripts/setup.sh" >&2
  exit 1
fi

"$PYTHON" -m pytest -q tests/test_usb_protocol.py

cc -std=c11 -Wall -Wextra   tests/update_protocol_test.c   firmware/platform/ch32v20x/ck5200_update_protocol.c   -o /tmp/ck5200_update_protocol_test

/tmp/ck5200_update_protocol_test
