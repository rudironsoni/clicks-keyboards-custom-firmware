#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-$ROOT/build/ck5200_qmk.bin}"

if [[ ! -f "$IMAGE" ]]; then
  echo "missing image: $IMAGE" >&2
  exit 1
fi

python3 "$ROOT/tools/validate_image.py" "$IMAGE"
python3 "$ROOT/tools/ck5200_usb.py" packets "$IMAGE"

cat <<'MSG'

The commands above did not write the device.
To perform the regular-USB write, rerun this script with FLASH=YES:

  FLASH=YES scripts/flash-qmk.sh

This is a custom image. The flasher will require both CK-5200 confirmation and
--allow-unknown-image.
MSG

if [[ "${FLASH:-NO}" != "YES" ]]; then
  exit 0
fi

python3 "$ROOT/tools/ck5200_usb.py" flash "$IMAGE" \
  --confirm CK-5200 \
  --allow-unknown-image
