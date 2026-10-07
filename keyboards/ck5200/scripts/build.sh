#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TOOLCHAIN_DIR="$(find "$ROOT/.toolchains" -type f -name riscv-none-elf-gcc -print -quit 2>/dev/null | xargs -r dirname || true)"
if [[ -n "$TOOLCHAIN_DIR" ]]; then
  export PATH="$TOOLCHAIN_DIR:$PATH"
fi

if [[ -d "$ROOT/.venv/bin" ]]; then
  export PATH="$ROOT/.venv/bin:$PATH"
fi

for command in cmake ninja python3; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "$command is required. Run: bash scripts/setup.sh" >&2
    exit 1
  fi
done

if ! command -v riscv-none-elf-gcc >/dev/null 2>&1 && ! command -v riscv-none-embed-gcc >/dev/null 2>&1; then
  echo "RISC-V compiler not found. Run: bash scripts/setup.sh" >&2
  exit 1
fi

if [[ ! -d external/qmk_firmware/.git || ! -d external/tinyusb/.git || ! -d external/ch32v20x/.git ]]; then
  echo "Source dependencies are missing. Run: bash scripts/setup.sh" >&2
  exit 1
fi

cmake -S . -B build -G Ninja   -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-gcc-toolchain.cmake   -DCMAKE_BUILD_TYPE=MinSizeRel

cmake --build build --target ck5200_qmk.elf

echo
echo "Built firmware:"
ls -lh   build/ck5200_qmk.bin   build/ck5200_qmk.elf   build/ck5200_qmk.hex   build/ck5200_qmk.map
