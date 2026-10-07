#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="$ROOT/.venv/bin/python"
STOCK_DIR="$ROOT/.stock"
STOCK_IMAGE="$STOCK_DIR/iKeyboard_CK-5200_V122_120.bin"
STOCK_URL="https://xinyi1.clicks.tech/CK-5200/iKeyboard_CK-5200_V122_120.bin"
STOCK_SHA256="8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"

if [[ ! -x "$PYTHON" ]]; then
  echo "Python environment not found. Run: bash scripts/setup.sh" >&2
  exit 1
fi

mkdir -p "$STOCK_DIR"

if [[ ! -f "$STOCK_IMAGE" ]]; then
  echo "Downloading stock CK-5200 firmware 1.2.2..."
  curl -L --fail --retry 3 -o "$STOCK_IMAGE" "$STOCK_URL"
fi

"$PYTHON" - "$STOCK_IMAGE" "$STOCK_SHA256" <<'PY'
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected = sys.argv[2]
actual = hashlib.sha256(path.read_bytes()).hexdigest()

if actual != expected:
    raise SystemExit(
        "Stock firmware SHA-256 mismatch\n"
        f"expected {expected}\n"
        f"actual   {actual}"
    )

print("Stock firmware SHA-256 OK")
PY

echo
echo "This will restore the official CK-5200 1.2.2 firmware."
echo "It only works while the USB updater interface is still reachable."
echo
read -r -p "Type STOCK-CK-5200 to continue: " answer

if [[ "$answer" != "STOCK-CK-5200" ]]; then
  echo "Cancelled."
  exit 1
fi

"$PYTHON" "$ROOT/tools/ck5200_usb.py" flash "$STOCK_IMAGE"   --confirm CK-5200
