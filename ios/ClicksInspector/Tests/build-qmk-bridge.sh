#!/usr/bin/env bash
set -euo pipefail
IOS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$IOS/../../keyboards/ck5200" && pwd)"
BUILD="$1"
Q="$ROOT/external/qmk_firmware"
. "$ROOT/scripts/pins.env"
test "$(git -C "$Q" rev-parse HEAD)" = "$QMK_COMMIT"
mkdir -p "$BUILD/qmk"
FLAGS=(-std=c11 -g -O1 -Wall -Wno-unused-parameter -DNO_PRINT -DNO_DEBUG -DFAST_TIMER_T_SIZE=32
  '-DQMK_KEYBOARD_H="ck5200.h"' '-DKEYMAP_C="keymap.c"' -DEEPROM_DRIVER -DEEPROM_TRANSIENT
  -include "$ROOT/firmware/keyboard/config.h")
if [[ -n "${2:-}" ]]; then
  FLAGS+=(-target "$2-apple-ios${IPHONEOS_DEPLOYMENT_TARGET:-17.0}-simulator"
    -isysroot "$(xcrun --sdk iphonesimulator --show-sdk-path)")
fi
for path in "$ROOT/tests/qmk_shim" "$ROOT/firmware/keyboard" "$ROOT/firmware/keyboard/keymaps/diagnostic" \
  "$ROOT/firmware/platform/ch32x035" "$Q" "$Q/quantum" "$Q/quantum/debounce" "$Q/quantum/wear_leveling" \
  "$Q/quantum/process_keycode" "$Q/quantum/logging" "$Q/quantum/keymap_extras" "$Q/quantum/sequencer" \
  "$Q/quantum/send_string" "$Q/tmk_core" "$Q/tmk_core/protocol" "$Q/platforms" "$Q/platforms/test" \
  "$Q/drivers" "$Q/drivers/eeprom" "$Q/lib" "$Q/lib/fnv"; do FLAGS+=(-I "$path"); done
SOURCES=("$IOS/Tests/QMKBridge.c" "$ROOT/firmware/platform/ch32x035/ck5200_keymap.c"
  "$ROOT/firmware/platform/ch32x035/ck5200_consumer.c"
  "$ROOT/firmware/keyboard/ck5200_debounce.c")
for path in quantum/action.c quantum/action_layer.c quantum/action_tapping.c quantum/action_util.c \
  quantum/keycode_config.c quantum/keymap_common.c quantum/keymap_introspection.c quantum/led.c quantum/quantum.c \
  quantum/keyboard.c quantum/matrix_common.c quantum/dynamic_keymap.c quantum/eeconfig.c quantum/sync_timer.c \
  quantum/bitwise.c quantum/deferred_exec.c quantum/caps_word.c quantum/bootmagic/bootmagic.c \
  quantum/process_keycode/process_magic.c quantum/logging/debug.c quantum/logging/print.c quantum/logging/sendchar.c \
  quantum/send_string/send_string.c drivers/eeprom/eeprom_driver.c \
  drivers/eeprom/eeprom_transient.c lib/fnv/hash_64a.c tmk_core/protocol/host.c tmk_core/protocol/report.c \
  tmk_core/protocol/usb_device_state.c platforms/test/timer.c; do SOURCES+=("$Q/$path"); done
OBJECTS=()
for index in "${!SOURCES[@]}"; do
  object="$BUILD/qmk/$index.o"
  xcrun clang "${FLAGS[@]}" -c "${SOURCES[$index]}" -o "$object"
  OBJECTS+=("$object")
done
xcrun ar rcs "$BUILD/libQMKBridge.a" "${OBJECTS[@]}"
