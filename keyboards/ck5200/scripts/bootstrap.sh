#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QMK_REPO=https://github.com/qmk/qmk_firmware.git
TINYUSB_REPO=https://github.com/hathach/tinyusb.git
OPENWCH_REPO=https://github.com/openwch/ch32v20x.git
OPENWCH_X035_REPO=https://github.com/openwch/ch32x035.git
. "$ROOT/scripts/pins.env"

mkdir -p external

clone_pin() {
  local url="$1" dir="$2" commit="$3"

  if [[ ! -d "$dir/.git" ]]; then
    git clone --filter=blob:none "$url" "$dir"
  fi

  git -C "$dir" fetch origin "$commit" --depth=1
  git -C "$dir" checkout --detach "$commit"
}

clone_pin "$QMK_REPO" external/qmk_firmware "$QMK_COMMIT"
clone_pin "$TINYUSB_REPO" external/tinyusb "$TINYUSB_COMMIT"
clone_pin "$OPENWCH_REPO" external/ch32v20x "$OPENWCH_COMMIT"
clone_pin "$OPENWCH_X035_REPO" external/ch32x035 "$OPENWCH_X035_COMMIT"

echo
echo "Source dependencies are ready."
echo "QMK:      $QMK_COMMIT"
echo "TinyUSB:  $TINYUSB_COMMIT"
echo "CH32V20x: $OPENWCH_COMMIT"
