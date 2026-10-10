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

# Consumer-key state machine: press/release semantics and 10-byte
# report packing against the descriptor contract.
cc -std=c11 -Wall -Wextra -I firmware/platform/ch32v20x \
  tests/ck5200_consumer_test.c \
  firmware/platform/ch32v20x/ck5200_consumer.c \
  -o /tmp/ck5200_consumer_test

/tmp/ck5200_consumer_test

cc -std=c11 -Wall -Wextra -I tests/stubs -DMATRIX_ROWS=6 -DMATRIX_COLS=6 \
  tests/ck5200_keymap_test.c \
  firmware/platform/ch32v20x/ck5200_keymap.c \
  -o /tmp/ck5200_keymap_test

/tmp/ck5200_keymap_test

# Full restore cycle: the real update handler and the real staging
# writer against a mocked flash backend, pushing the complete stock and
# custom images through A1/A2/A3/A0, plus bounds and failure injection.
cc -std=c11 -Wall -Wextra -DCK5200_STAGING_HOST_TEST -I tests/stubs \
  tests/ck5200_restore_test.c \
  firmware/platform/ch32v20x/ck5200_update_protocol.c \
  firmware/platform/ch32v20x/ck5200_staging.c \
  -o /tmp/ck5200_restore_test

/tmp/ck5200_restore_test .stock/iKeyboard_CK-5200_V122_120.bin build/ck5200_qmk.bin

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
