#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-$ROOT/build/ck5200_qmk.bin}"
PYTHON="$ROOT/.venv/bin/python"

if [[ ! -x "$PYTHON" ]]; then
  echo "Python environment not found. Run: bash scripts/setup.sh" >&2
  exit 1
fi

if [[ ! -f "$IMAGE" ]]; then
  echo "Firmware not found: $IMAGE" >&2
  echo "Build it first with: bash scripts/build.sh" >&2
  exit 1
fi

"$PYTHON" "$ROOT/tools/validate_image.py" "$IMAGE" --elf "${IMAGE%.bin}.elf"
"$PYTHON" "$ROOT/tools/ck5200_usb.py" packets "$IMAGE"

echo
echo "Dry run only. This command never opens USB, including when FLASH=YES."
echo "Custom installation remains blocked until stock transport and recovery are verified."
