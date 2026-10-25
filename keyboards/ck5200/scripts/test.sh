#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TEST_BUILD="$(mktemp -d "${TMPDIR:-/tmp}/ck5200-tests.XXXXXX")"
trap 'rm -rf "$TEST_BUILD"' EXIT

PYTHON="$ROOT/.venv/bin/python"
if [[ ! -x "$PYTHON" ]]; then
  echo "Python environment not found. Run: bash scripts/setup.sh" >&2
  exit 1
fi

"$PYTHON" -m pytest -q tests/test_usb_protocol.py
"$PYTHON" -m pytest -q tests/test_keymap_layout.py
"$PYTHON" -m pytest -q tests/test_validate_image.py

cc -std=c11 -Wall -Wextra -DCK5200_IDENTITY_HOST_TEST \
  -I external/tinyusb/src -I firmware/platform/ch32x035 -I firmware/keyboard \
  tests/ck5200_descriptors_test.c -o "$TEST_BUILD/ck5200_descriptors_test"
"$TEST_BUILD/ck5200_descriptors_test" .stock/iKeyboard_CK-5200_V122_120.bin

cc -std=c11 -Wall -Wextra   tests/update_protocol_test.c   firmware/platform/ch32x035/ck5200_update_protocol.c   -o "$TEST_BUILD/ck5200_update_protocol_test"

"$TEST_BUILD/ck5200_update_protocol_test"

# Backlight driver: real TIM1 register writes against the stock decode.
cc -std=c11 -Wall -Wextra -DCK5200_BACKLIGHT_HOST_TEST -I tests/stubs -I firmware/platform/ch32x035 \
  tests/ck5200_backlight_test.c \
  firmware/platform/ch32x035/ck5200_backlight.c \
  -o "$TEST_BUILD/ck5200_backlight_test"

"$TEST_BUILD/ck5200_backlight_test"

cc -std=c11 -Wall -Wextra tests/ck5200_watchdog_test.c -o "$TEST_BUILD/ck5200_watchdog_test"
"$TEST_BUILD/ck5200_watchdog_test"

cc -std=c11 -Wall -Wextra -I tests/stubs -DMATRIX_ROWS=6 -DMATRIX_COLS=6 \
  tests/ck5200_keymap_test.c \
  firmware/platform/ch32x035/ck5200_keymap.c \
  -o "$TEST_BUILD/ck5200_keymap_test"

"$TEST_BUILD/ck5200_keymap_test"

# Full restore cycle: the real update handler and the real staging
# writer against a mocked flash backend, pushing the complete stock and
# custom images through A1/A2/A3/A0, plus bounds and failure injection.
cc -std=c11 -Wall -Wextra -DCK5200_STAGING_HOST_TEST -I tests/stubs -I external/sha-2 \
  tests/ck5200_restore_test.c \
  firmware/platform/ch32x035/ck5200_update_protocol.c \
  firmware/platform/ch32x035/ck5200_staging.c \
  external/sha-2/sha-256.c \
  -o "$TEST_BUILD/ck5200_restore_test"

"$TEST_BUILD/ck5200_restore_test" .stock/iKeyboard_CK-5200_V122_120.bin build/ck5200_qmk.bin

# Apple session stack: real iap2.c against a scripted iPhone and a fake
# auth chip; asserts every outgoing EP2 packet byte-for-byte.
cc -std=c11 -Wall -Wextra -DCK5200_IDENTITY_HOST_TEST \
  -I external/tinyusb/src -I firmware/platform/ch32x035 -I firmware/keyboard \
  tests/iap2_session_test.c \
  firmware/platform/ch32x035/iap2.c \
  firmware/usb/usb_descriptors.c firmware/usb/vendor_driver.c \
  external/tinyusb/src/class/vendor/vendor_device.c \
  -o "$TEST_BUILD/ck5200_iap2_session_test"

"$TEST_BUILD/ck5200_iap2_session_test"

# Auth I2C driver: real iap2_auth.c against a waveform-level chip model.
cc -std=c11 -Wall -Wextra -DIAP2_AUTH_HOST_TEST -I tests/stubs -I firmware/platform/ch32x035 \
  tests/iap2_auth_test.c \
  firmware/platform/ch32x035/iap2_auth.c \
  -o "$TEST_BUILD/ck5200_iap2_auth_test"

"$TEST_BUILD/ck5200_iap2_auth_test"

# QMK core + protocol behavior harnesses: the REAL pinned QMK core, the
# REAL CK-5200 modules, and (protocol) the REAL protocol.c against a
# TinyUSB-0.21-faithful completion stub, with manual time. Ported from the
# 2026-10-11 adversarial review, whose harnesses caught the HID completion
# freeze, the consumer byte order, the dead one-shot layer, and the
# wear-leveling contract violation. The X035 re-target (2026-10-12) moved
# persistence to the runtime-only transient EEPROM backend.
Q="$ROOT/external/qmk_firmware"
QSHIM="$ROOT/tests/qmk_shim"
QFLAGS="-std=c11 -g -O1 -Wall -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer"
QINC="-I $QSHIM -I $ROOT/firmware/keyboard -I $ROOT/firmware/keyboard/keymaps/diagnostic \
 -I $ROOT/firmware/platform/ch32x035 \
 -I $Q -I $Q/quantum -I $Q/quantum/debounce -I $Q/quantum/wear_leveling \
 -I $Q/quantum/process_keycode -I $Q/quantum/logging -I $Q/quantum/keymap_extras \
 -I $Q/quantum/sequencer -I $Q/quantum/send_string \
 -I $Q/tmk_core -I $Q/tmk_core/protocol -I $Q/platforms -I $Q/platforms/test \
 -I $Q/drivers -I $Q/drivers/eeprom -I $Q/lib -I $Q/lib/fnv"
QDEFS="-DNO_PRINT -DNO_DEBUG -DFAST_TIMER_T_SIZE=32 \
 -DQMK_KEYBOARD_H=\"ck5200.h\" -DKEYMAP_C=\"keymap.c\" \
 -DEEPROM_DRIVER -DEEPROM_TRANSIENT \
 -include $ROOT/firmware/keyboard/config.h"
QCORE="$Q/quantum/action.c $Q/quantum/action_layer.c $Q/quantum/action_tapping.c \
 $Q/quantum/action_util.c $Q/quantum/keycode_config.c $Q/quantum/keymap_common.c \
 $Q/quantum/keymap_introspection.c $Q/quantum/led.c $Q/quantum/quantum.c \
 $Q/quantum/keyboard.c $Q/quantum/matrix_common.c $Q/quantum/dynamic_keymap.c \
 $Q/quantum/eeconfig.c $Q/quantum/sync_timer.c $Q/quantum/bitwise.c \
 $Q/quantum/deferred_exec.c $Q/quantum/caps_word.c $Q/quantum/bootmagic/bootmagic.c \
 $Q/quantum/process_keycode/process_magic.c $Q/quantum/logging/debug.c \
 $Q/quantum/logging/print.c $Q/quantum/logging/sendchar.c \
 $ROOT/firmware/keyboard/ck5200_debounce.c \
 $Q/quantum/send_string/send_string.c \
 $Q/drivers/eeprom/eeprom_driver.c $Q/drivers/eeprom/eeprom_transient.c \
 $Q/lib/fnv/hash_64a.c $Q/tmk_core/protocol/host.c $Q/tmk_core/protocol/report.c \
 $Q/tmk_core/protocol/usb_device_state.c $Q/platforms/test/timer.c"
QCK="$ROOT/firmware/platform/ch32x035/ck5200_consumer.c \
 $ROOT/firmware/platform/ch32x035/ck5200_keymap.c"

# Consumer-key state changes send through QMK's host interface.
cc $QFLAGS $QINC $QDEFS -o "$TEST_BUILD/ck5200_consumer_test" \
  tests/ck5200_consumer_test.c firmware/platform/ch32x035/ck5200_consumer.c
"$TEST_BUILD/ck5200_consumer_test"

cc $QFLAGS $QINC $QDEFS -o "$TEST_BUILD/ck5200_qmk_core_test" \
  tests/ck5200_qmk_core_test.c $QCORE $QCK

liveness_failures=0
"$TEST_BUILD/ck5200_qmk_core_test" || liveness_failures=$((liveness_failures + 1))

cc $QFLAGS $QINC $QDEFS -o "$TEST_BUILD/ck5200_protocol_task_test" \
  tests/ck5200_protocol_task_test.c \
  firmware/platform/ch32x035/protocol.c firmware/keyboard/matrix.c $QCORE $QCK

"$TEST_BUILD/ck5200_protocol_task_test" || liveness_failures=$((liveness_failures + 1))
if (( liveness_failures > 0 )); then
  echo "Both liveness harnesses ran; $liveness_failures failure(s)" >&2
  exit 1
fi

# Real selected main, transport tasks, Apple session, and constrained staging.
M2_BUILD="$TEST_BUILD"
cc $QFLAGS $QINC $QDEFS -Dmain=ck5200_main -DCK5200_RESCUE_HOST_TEST -c \
  firmware/platform/ch32x035/main.c -o "$M2_BUILD/main.o"
cc $QFLAGS $QINC $QDEFS -DCK5200_STAGING_HOST_TEST -DCK5200_IDENTITY_HOST_TEST -I tests/stubs -I external/sha-2 \
  tests/ck5200_rescue_test.c "$M2_BUILD/main.o" \
  firmware/platform/ch32x035/protocol.c \
  firmware/platform/ch32x035/iap2.c \
  external/sha-2/sha-256.c \
  firmware/platform/ch32x035/ck5200_staging.c \
  firmware/platform/ch32x035/ck5200_update_protocol.c \
  $QCORE $QCK -o "$M2_BUILD/rescue_test"
for mode in normal rescue early-failure closed-session watchdog-jump watchdog-packet watchdog-ep3 watchdog-scan-stall watchdog-transport-stall watchdog-busy watchdog-suspend; do
  UBSAN_OPTIONS=halt_on_error=1 "$M2_BUILD/rescue_test" "$mode" .stock/iKeyboard_CK-5200_V122_120.bin
done

# Family-gate negative test (review 2026-10-12, finding 6): compiling the
# real family_gate.c against a deliberately wrong-family stub MUST fail.
# The positive side is proven by the firmware build itself (family_gate.c
# compiles against the real SDK header).
if cc -std=c11 -I tests/family_gate_negative -fsyntax-only \
  firmware/platform/ch32x035/family_gate.c >/dev/null 2>&1; then
  echo "FAMILY GATE NEGATIVE TEST FAILED: wrong-family constants compiled cleanly" >&2
  exit 1
fi
echo "family gate rejects wrong-family constants"
