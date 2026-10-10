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
"$PYTHON" -m pytest -q tests/test_keymap_layout.py

cc -std=c11 -Wall -Wextra   tests/update_protocol_test.c   firmware/platform/ch32v20x/ck5200_update_protocol.c   -o /tmp/ck5200_update_protocol_test

/tmp/ck5200_update_protocol_test

# Backlight driver: real TIM1 register writes against the stock decode.
cc -std=c11 -Wall -Wextra -DCK5200_BACKLIGHT_HOST_TEST -I tests/stubs -I firmware/platform/ch32v20x \
  tests/ck5200_backlight_test.c \
  firmware/platform/ch32v20x/ck5200_backlight.c \
  -o /tmp/ck5200_backlight_test

/tmp/ck5200_backlight_test

cc -std=c11 -Wall -Wextra -I tests/stubs -DMATRIX_ROWS=6 -DMATRIX_COLS=6 \
  tests/ck5200_keymap_test.c \
  firmware/platform/ch32v20x/ck5200_keymap.c \
  -o /tmp/ck5200_keymap_test

/tmp/ck5200_keymap_test

# Apple session stack: real iap2.c against a scripted iPhone and a fake
# auth chip; asserts every outgoing EP2 packet byte-for-byte.
cc -std=c11 -Wall -Wextra -I tests/stubs -I firmware/platform/ch32v20x \
  tests/iap2_session_test.c \
  firmware/platform/ch32v20x/iap2.c \
  -o /tmp/ck5200_iap2_session_test

/tmp/ck5200_iap2_session_test

# Auth I2C driver: real iap2_auth.c against a waveform-level chip model.
cc -std=c11 -Wall -Wextra -DIAP2_AUTH_HOST_TEST -I tests/stubs -I firmware/platform/ch32v20x \
  tests/iap2_auth_test.c \
  firmware/platform/ch32v20x/iap2_auth.c \
  -o /tmp/ck5200_iap2_auth_test

/tmp/ck5200_iap2_auth_test
