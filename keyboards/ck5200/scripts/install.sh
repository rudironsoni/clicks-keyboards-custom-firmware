#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-$ROOT/build/ck5200_qmk.bin}"

if [[ ! -f "$IMAGE" ]]; then
  echo "Firmware not found: $IMAGE" >&2
  echo "Build it first with: bash scripts/build.sh" >&2
  exit 1
fi

echo "This will flash custom QMK firmware to the connected CK-5200."
echo "The regular USB restore path only works if the keyboard still boots far enough to expose the updater."
echo
read -r -p "Type CK-5200 to continue: " answer

if [[ "$answer" != "CK-5200" ]]; then
  echo "Cancelled."
  exit 1
fi

FLASH=YES bash "$ROOT/scripts/flash-qmk.sh" "$IMAGE"
