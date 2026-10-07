#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TOOLCHAIN_VERSION="15.2.0-1"
TOOLCHAIN_ROOT="$ROOT/.toolchains"
TOOLCHAIN_DIR="$TOOLCHAIN_ROOT/xpack-riscv-none-elf-gcc-$TOOLCHAIN_VERSION"

have_compiler() {
  command -v riscv-none-elf-gcc >/dev/null 2>&1 ||   [[ -x "$TOOLCHAIN_DIR/bin/riscv-none-elf-gcc" ]]
}

install_toolchain() {
  local os arch package sha url archive tmp

  os="$(uname -s)"
  arch="$(uname -m)"

  case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64)
      package="darwin-arm64"
      sha="6588e8351455fad8aca37551f0e5a5543f3346bfa9a837cf03cbd3bdd4989f8f"
      ;;
    Darwin:x86_64)
      package="darwin-x64"
      sha="98e83f097b10163869dabffd58389ac8e4eb41bae0f67124569158655be593ea"
      ;;
    Linux:x86_64)
      package="linux-x64"
      sha="aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758"
      ;;
    Linux:arm64|Linux:aarch64)
      package="linux-arm64"
      sha="4e60e2a54c16385e4e2476d08240f857495d5a61609d97e1ee49f72875a6ec1e"
      ;;
    *)
      echo "No automatic RISC-V toolchain install for $os/$arch." >&2
      echo "Install riscv-none-elf-gcc yourself, then rerun this script." >&2
      exit 1
      ;;
  esac

  mkdir -p "$TOOLCHAIN_ROOT"
  archive="xpack-riscv-none-elf-gcc-$TOOLCHAIN_VERSION-$package.tar.gz"
  url="https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$TOOLCHAIN_VERSION/$archive"
  tmp="$TOOLCHAIN_ROOT/$archive"

  echo "Downloading RISC-V compiler..."
  curl -L --fail --retry 3 -o "$tmp" "$url"

  python3 - "$tmp" "$sha" <<'PY'
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected = sys.argv[2]
actual = hashlib.sha256(path.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f"toolchain SHA-256 mismatch\nexpected {expected}\nactual   {actual}")
print("Toolchain SHA-256 OK")
PY

  tar -xzf "$tmp" -C "$TOOLCHAIN_ROOT"
  rm -f "$tmp"

  if [[ ! -x "$TOOLCHAIN_DIR/bin/riscv-none-elf-gcc" ]]; then
    echo "Compiler was downloaded but not found at:" >&2
    echo "  $TOOLCHAIN_DIR/bin/riscv-none-elf-gcc" >&2
    exit 1
  fi
}

if ! have_compiler; then
  install_toolchain
fi

if [[ -x "$TOOLCHAIN_DIR/bin/riscv-none-elf-gcc" ]]; then
  export PATH="$TOOLCHAIN_DIR/bin:$PATH"
fi

COMPILER="$(command -v riscv-none-elf-gcc || command -v riscv-none-embed-gcc || true)"
if [[ -z "$COMPILER" ]]; then
  echo "RISC-V compiler not found after setup." >&2
  exit 1
fi

echo "Compiler: $COMPILER"
"$COMPILER" --version | head -n 1

if [[ ! -d .venv ]]; then
  python3 -m venv .venv
fi

.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r tools/requirements.txt pytest

bash scripts/bootstrap.sh

echo
echo "Setup complete."
echo "Build with:"
echo "  bash scripts/build.sh"
