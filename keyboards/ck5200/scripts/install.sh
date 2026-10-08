#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-$ROOT/build/ck5200_qmk.bin}"

if [[ ! -f "$IMAGE" ]]; then
  echo "Firmware not found: $IMAGE" >&2
  echo "Build it first with: bash scripts/build.sh" >&2
  exit 1
fi

bash "$ROOT/scripts/flash-qmk.sh" "$IMAGE"
echo "Installation blocked: no verified recovery procedure for this iPhone 15 Pro Max case." >&2
echo "Read $ROOT/docs/RECOVERY.md before any hardware write." >&2
exit 1
