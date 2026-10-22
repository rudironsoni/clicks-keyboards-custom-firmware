#!/usr/bin/env bash
# Build the CK-5200 stock-extension RESEARCH image (offline artifact).
#
# Produces, under build/stock_extension/:
#   v122_patched.bin      - V122 with the 8-byte registration redirect
#   ck5200_extension.bin  - the fully linked image (V122 + extension)
#   build-report.txt      - sizes and hashes
#
# RESEARCH ARTIFACT ONLY. Never installed; the R3 recovery gate is
# unchanged. Sizes are the milestone's storage measurements.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/stock_extension"
mkdir -p "$OUT"

shopt -s nullglob
TC_DIRS=("$ROOT"/.toolchains/xpack-riscv-none-elf-gcc-*)
shopt -u nullglob
if [ ${#TC_DIRS[@]} -eq 0 ]; then
    echo "error: pinned riscv toolchain not installed (scripts/setup.sh)" >&2
    exit 2
fi
TC_DIR="${TC_DIRS[0]}"
CC="$TC_DIR/bin/riscv-none-elf-gcc"
OBJCOPY="$TC_DIR/bin/riscv-none-elf-objcopy"
SIZE="$TC_DIR/bin/riscv-none-elf-size"
NM="$TC_DIR/bin/riscv-none-elf-nm"
OBJDUMP="$TC_DIR/bin/riscv-none-elf-objdump"
HOSTCC="${HOSTCC:-cc}"
STOCK="$ROOT/.stock/iKeyboard_CK-5200_V122_120.bin"
EXT_DIR="$ROOT/stock_extension"
TESTS_DIR="$ROOT/tests"

# 1. Patch the pinned V122 (hash-gated by the patcher itself).
python3 "$ROOT/tools/stock_extension_patch.py" "$STOCK" \
    "$OUT/v122_patched.bin" "$OUT/patch_manifest.json"

# 2. Generate the serialized default keymap record (host tool).
"$HOSTCC" -std=c99 -Os -I "$EXT_DIR" -I "$TESTS_DIR" \
    "$EXT_DIR/ck5200_keymap_engine.c" \
    "$EXT_DIR/ck5200_keymap_serialize.c" \
    "$EXT_DIR/ck5200_gen_record.c" -o "$OUT/gen_record"
"$OUT/gen_record" > "$OUT/ck5200_default_record.c"

# 3. Wrap the patched V122 as an object section at its native base.
"$OBJCOPY" -I binary -O elf32-littleriscv -B riscv \
    --rename-section .data=.v122blob,alloc,load,readonly,code,contents \
    "$OUT/v122_patched.bin" "$OUT/v122_blob.o"

# 4. Compile the extension for the target.
CFLAGS=(-std=c99 -Os -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medlow
        -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
        -msmall-data-limit=0 -mno-relax
        -I "$EXT_DIR")
"$CC" "${CFLAGS[@]}" -c "$EXT_DIR/ck5200_keymap_engine.c" -o "$OUT/engine.o"
"$CC" "${CFLAGS[@]}" -c "$EXT_DIR/ck5200_keymap_serialize.c" -o "$OUT/serialize.o"
"$CC" "${CFLAGS[@]}" -c "$EXT_DIR/ck5200_extension_shim.c" -o "$OUT/shim.o"
"$CC" "${CFLAGS[@]}" -c "$OUT/ck5200_default_record.c" -o "$OUT/record.o"
"$CC" "${CFLAGS[@]}" -c "$EXT_DIR/ck5200_freestanding.c" -o "$OUT/freestanding.o"

# 5. Link the complete image.
"$CC" -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medlow -mno-relax -nostdlib \
    -T "$EXT_DIR/extension.ld" \
    -Wl,-Map,"$OUT/ck5200_extension.map" \
    "$OUT/v122_blob.o" "$OUT/shim.o" "$OUT/engine.o" "$OUT/serialize.o" \
    "$OUT/record.o" "$OUT/freestanding.o" \
    -o "$OUT/ck5200_extension.elf"
"$NM" -n "$OUT/ck5200_extension.elf" > "$OUT/symbols.txt"
# The new hook targets come from the linked symbols, not guessed offsets.
# Update the fixed-size stock section in the ELF, then extract the BIN.
STARTUP_TARGET="$(awk '$3 == "ck_extension_init" {print "0x" $1}' "$OUT/symbols.txt")"
TICK_TARGET="$(awk '$3 == "ck_extension_tick" {print "0x" $1}' "$OUT/symbols.txt")"
python3 "$ROOT/tools/stock_extension_patch.py" "$STOCK" \
    "$OUT/v122_patched.bin" "$OUT/patch_manifest.json" \
    --startup-target "$STARTUP_TARGET" --tick-target "$TICK_TARGET"
python3 "$ROOT/tools/stock_extension_patch.py" "$OUT/v122_patched.bin" \
    "$OUT/v122_reversed.bin" "$OUT/reverse_manifest.json" --reverse \
    --startup-target "$STARTUP_TARGET" --tick-target "$TICK_TARGET"
cmp "$STOCK" "$OUT/v122_reversed.bin"
"$OBJCOPY" --update-section .v122blob="$OUT/v122_patched.bin" "$OUT/ck5200_extension.elf"
"$OBJDUMP" -d "$OUT/ck5200_extension.elf" > "$OUT/ck5200_extension.disassembly.txt"
python3 - "$OUT/symbols.txt" "$OUT/ck5200_extension.disassembly.txt" <<'PY'
import pathlib, re, sys
symbols, disassembly = (pathlib.Path(p).read_text() for p in sys.argv[1:])
assert re.search(r"^00006900 T ck_extension_builder$", symbols, re.M), symbols
assert re.search(r"^00006900 <ck_extension_builder>:", disassembly, re.M)
PY
"$OBJCOPY" -O binary "$OUT/ck5200_extension.elf" "$OUT/ck5200_extension.bin"

# 6. Report sizes; enforce the capacity budget.
"$SIZE" "$OUT/ck5200_extension.elf" | tee "$OUT/sizes.txt"
python3 - "$OUT/ck5200_extension.bin" "$OUT/build-report.txt" <<'PY'
import hashlib, sys, pathlib
img = pathlib.Path(sys.argv[1])
data = img.read_bytes()
total = len(data)
v122 = 18648
gap = 0x6900 - (0x2000 + v122)
ext = total - (0x6900 - 0x2000)
limit = 0x8A00 - 0x2000          # app-area capacity 27,136 bytes
report = [
    f"total image: {total} bytes (extent 0x{0x2000 + total:04X})",
    f"V122 base:   {v122} bytes",
    f"alignment gap: {gap} bytes",
    f"extension:   {ext} bytes (at 0x6900)",
    f"capacity:    {limit} bytes; headroom {limit - total}",
    f"sha256: {hashlib.sha256(data).hexdigest()}",
]
assert total <= limit, f"image exceeds app-area capacity: {total} > {limit}"
pathlib.Path(sys.argv[2]).write_text("\n".join(report) + "\n")
print("\n".join(report))
PY
python3 - "$OUT" <<'PY'
import hashlib, pathlib, sys
out = pathlib.Path(sys.argv[1])
names = ['ck5200_extension.' + suffix for suffix in ('bin', 'elf', 'map', 'disassembly.txt')]
(out / 'SHA256SUMS').write_text(''.join(
    f'{hashlib.sha256((out / name).read_bytes()).hexdigest()}  {name}\n' for name in names))
PY
echo "research image built (NOT authorized for installation; R3 gate unchanged)"
