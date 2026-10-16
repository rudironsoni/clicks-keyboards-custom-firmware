#!/usr/bin/env bash
# Build a host-only archive. No Xcode, network fetch, or firmware cross-compiler.
# Usage: bash Tests/build-stock-manager-bridge.sh [output-archive]
# CC and AR may select generic host tools. CFLAGS/LDFLAGS are not needed by Swift.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../keyboards/ck5200" && pwd)"
Q="$ROOT/external/qmk_firmware"
SHA="$ROOT/external/sha-2"
OUT="${1:-$HERE/../build/libStockManagerBridge.a}"
source "$ROOT/scripts/pins.env"
verify_pin() {
    local dir="$1" pin="$2" actual
    actual="$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)"
    if [[ "$actual" != "$pin" ]]; then
        echo "$dir is at ${actual:-nothing}; expected $pin. Run keyboards/ck5200/scripts/bootstrap.sh separately." >&2
        exit 1
    fi
    if [[ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]]; then
        echo "$dir has tracked changes; a clean pinned dependency is required." >&2
        exit 1
    fi
}
verify_pin "$Q" "$QMK_COMMIT"
verify_pin "$SHA" "$SHA256_COMMIT"
mkdir -p "$(dirname "$OUT")"
BUILD="$(mktemp -d "${TMPDIR:-/tmp}/stock-manager-bridge.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
FLAGS=(-std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter
    -DNO_PRINT -DNO_DEBUG -DFAST_TIMER_T_SIZE=32
    '-DQMK_KEYBOARD_H="ck5200.h"' '-DKEYMAP_C="keymap.c"'
    -DEEPROM_DRIVER -DEEPROM_TRANSIENT -DCK5200_STAGING_HOST_TEST
    -include "$ROOT/firmware/keyboard/config.h")
INCLUDES=("$ROOT/tests/qmk_shim" "$ROOT/firmware/keyboard"
    "$ROOT/firmware/keyboard/keymaps/diagnostic" "$ROOT/firmware/platform/ch32x035"
    "$Q" "$Q/quantum" "$Q/quantum/debounce" "$Q/quantum/wear_leveling"
    "$Q/quantum/process_keycode" "$Q/quantum/logging" "$Q/quantum/keymap_extras"
    "$Q/quantum/sequencer" "$Q/quantum/send_string" "$Q/tmk_core"
    "$Q/tmk_core/protocol" "$Q/platforms" "$Q/platforms/test" "$Q/drivers"
    "$Q/drivers/eeprom" "$Q/lib" "$Q/lib/fnv" "$ROOT/tests/stubs" "$SHA")
for inc in "${INCLUDES[@]}"; do FLAGS+=(-I "$inc"); done
QCORE=(quantum/action.c quantum/action_layer.c quantum/action_tapping.c
    quantum/action_util.c quantum/keycode_config.c quantum/keymap_common.c
    quantum/keymap_introspection.c quantum/led.c quantum/quantum.c
    quantum/keyboard.c quantum/matrix_common.c quantum/dynamic_keymap.c
    quantum/eeconfig.c quantum/sync_timer.c quantum/bitwise.c
    quantum/deferred_exec.c quantum/caps_word.c quantum/bootmagic/bootmagic.c
    quantum/process_keycode/process_magic.c quantum/logging/debug.c
    quantum/logging/print.c quantum/logging/sendchar.c quantum/debounce/sym_defer_g.c
    quantum/send_string/send_string.c drivers/eeprom/eeprom_driver.c
    drivers/eeprom/eeprom_transient.c lib/fnv/hash_64a.c tmk_core/protocol/host.c
    tmk_core/protocol/report.c tmk_core/protocol/usb_device_state.c platforms/test/timer.c)
SOURCES=("$HERE/StockManagerBridge.c" "$SHA/sha-256.c")
for file in protocol iap2 ck5200_staging ck5200_update_protocol ck5200_consumer ck5200_keymap; do
    SOURCES+=("$ROOT/firmware/platform/ch32x035/$file.c")
done
for file in "${QCORE[@]}"; do SOURCES+=("$Q/$file"); done
CC="${CC:-cc}"
AR="${AR:-ar}"
"$CC" "${FLAGS[@]}" -Dmain=ck5200_main -DCK5200_RESCUE_HOST_TEST -c \
    "$ROOT/firmware/platform/ch32x035/main.c" -o "$BUILD/main.o"
OBJECTS=("$BUILD/main.o")
index=0
for file in "${SOURCES[@]}"; do
    object="$BUILD/source-$index.o"
    "$CC" "${FLAGS[@]}" -c "$file" -o "$object"
    OBJECTS+=("$object")
    index=$((index + 1))
done
"$AR" rcs "$BUILD/libStockManagerBridge.a" "${OBJECTS[@]}"
mv "$BUILD/libStockManagerBridge.a" "$OUT"
echo "$OUT"
