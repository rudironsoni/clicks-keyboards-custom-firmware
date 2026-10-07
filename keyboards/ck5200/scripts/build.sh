#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v cmake >/dev/null; then
  echo "cmake is required" >&2
  exit 1
fi

if ! command -v riscv-none-elf-gcc >/dev/null && ! command -v riscv-none-embed-gcc >/dev/null; then
  cat >&2 <<'MSG'
A RISC-V embedded GCC toolchain is required.
Expected executable: riscv-none-elf-gcc or riscv-none-embed-gcc.
MSG
  exit 1
fi

cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-gcc-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --target ck5200_qmk.elf

printf '\nBuilt:\n'
ls -lh build/ck5200_qmk.elf build/ck5200_qmk.bin build/ck5200_qmk.hex build/ck5200_qmk.map
