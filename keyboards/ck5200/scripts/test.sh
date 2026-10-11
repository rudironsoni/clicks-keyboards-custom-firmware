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

# QMK core + protocol behavior harnesses: the REAL pinned QMK core, the
# REAL CK-5200 modules, and (protocol) the REAL protocol.c against a
# TinyUSB-0.21-faithful completion stub, on a host CH32-flash model with
# manual time. Ported from the 2026-10-11 adversarial review, whose
# harnesses caught the HID completion freeze, the consumer byte order,
# the dead one-shot layer, and the wear-leveling contract violation.
Q="$ROOT/external/qmk_firmware"
QSHIM="$ROOT/tests/qmk_shim"
QFLAGS="-std=c11 -g -O1 -Wall -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer"
QINC="-I $QSHIM -I $ROOT/firmware/keyboard -I $ROOT/firmware/keyboard/keymaps/diagnostic \
 -I $ROOT/firmware/platform/ch32v20x \
 -I $Q -I $Q/quantum -I $Q/quantum/debounce -I $Q/quantum/wear_leveling \
 -I $Q/quantum/process_keycode -I $Q/quantum/logging -I $Q/quantum/keymap_extras \
 -I $Q/quantum/sequencer -I $Q/quantum/send_string \
 -I $Q/tmk_core -I $Q/tmk_core/protocol -I $Q/platforms -I $Q/platforms/test \
 -I $Q/drivers -I $Q/drivers/eeprom -I $Q/lib -I $Q/lib/fnv"
QDEFS="-DNO_PRINT -DNO_DEBUG -DFAST_TIMER_T_SIZE=32 \
 -DQMK_KEYBOARD_H=\"ck5200.h\" -DKEYMAP_C=\"keymap.c\" \
 -DEEPROM_DRIVER -DCK5200_WL_HOST_TEST \
 -include $ROOT/firmware/keyboard/config.h"
QCORE="$Q/quantum/action.c $Q/quantum/action_layer.c $Q/quantum/action_tapping.c \
 $Q/quantum/action_util.c $Q/quantum/keycode_config.c $Q/quantum/keymap_common.c \
 $Q/quantum/keymap_introspection.c $Q/quantum/led.c $Q/quantum/quantum.c \
 $Q/quantum/keyboard.c $Q/quantum/matrix_common.c $Q/quantum/dynamic_keymap.c \
 $Q/quantum/eeconfig.c $Q/quantum/sync_timer.c $Q/quantum/bitwise.c \
 $Q/quantum/deferred_exec.c $Q/quantum/caps_word.c $Q/quantum/bootmagic/bootmagic.c \
 $Q/quantum/process_keycode/process_magic.c $Q/quantum/logging/debug.c \
 $Q/quantum/logging/print.c $Q/quantum/logging/sendchar.c \
 $Q/quantum/debounce/sym_defer_g.c $Q/quantum/wear_leveling/wear_leveling.c \
 $Q/quantum/send_string/send_string.c \
 $Q/drivers/eeprom/eeprom_driver.c $Q/drivers/eeprom/eeprom_wear_leveling.c \
 $Q/lib/fnv/hash_64a.c $Q/tmk_core/protocol/host.c $Q/tmk_core/protocol/report.c \
 $Q/tmk_core/protocol/usb_device_state.c $Q/platforms/test/timer.c"
QCK="$ROOT/firmware/platform/ch32v20x/ck5200_consumer.c \
 $ROOT/firmware/platform/ch32v20x/wear_leveling_ch32v20x.c \
 $ROOT/firmware/platform/ch32v20x/ck5200_keymap.c"

cc $QFLAGS $QINC $QDEFS -o /tmp/ck5200_qmk_core_test \
  tests/ck5200_qmk_core_test.c tests/qmk_shim/host_flash_model.c $QCORE $QCK

/tmp/ck5200_qmk_core_test

cc $QFLAGS $QINC $QDEFS -o /tmp/ck5200_protocol_task_test \
  tests/ck5200_protocol_task_test.c tests/qmk_shim/host_flash_model.c \
  firmware/platform/ch32v20x/protocol.c $QCORE $QCK

/tmp/ck5200_protocol_task_test
