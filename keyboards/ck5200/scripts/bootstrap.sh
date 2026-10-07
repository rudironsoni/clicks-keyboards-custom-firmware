#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QMK_REPO=https://github.com/qmk/qmk_firmware.git
QMK_COMMIT=3d4da6de29c8635c9cd232ce456d8fec8d31921b
TINYUSB_REPO=https://github.com/hathach/tinyusb.git
TINYUSB_COMMIT=efa3e132235aefa4ef49d31c746c0c5f0c0e0bc9
OPENWCH_REPO=https://github.com/openwch/ch32v20x.git
OPENWCH_COMMIT=baef0054588f4826548429ff4e7a9257d752ef2f

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

cat <<MSG
Dependencies pinned:
  QMK      $QMK_COMMIT
  TinyUSB  $TINYUSB_COMMIT
  CH32V20x $OPENWCH_COMMIT

Next:
  scripts/build.sh
MSG
