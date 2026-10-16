#!/usr/bin/env bash
# Two clean builds; no device access. Raw maps contain GCC temporary names.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
FIRST="$(mktemp -d "${TMPDIR:-/tmp}/ck5200-repro.XXXXXX")"
trap 'rm -rf "$FIRST"' EXIT

for pass in 1 2; do
  if [[ -f build/build.ninja ]]; then cmake --build build --target clean; fi
  bash scripts/build.sh
  .venv/bin/python - "$ROOT" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
text = (root / "build/ck5200_qmk.map").read_text()
# Only host paths and GCC's random LTO temporary basename are normalized.
text = text.replace(str(root), "<ROOT>")
text = re.sub(r"\S*/cc[A-Za-z0-9]+(?=\.ltrans\d+\.ltrans\.o)", "<LTO>", text)
(root / "build/ck5200_qmk.map.normalized").write_text(text)
PY
  if [[ "$pass" == 1 ]]; then
    cp build/ck5200_qmk.bin build/ck5200_qmk.elf build/ck5200_qmk.map.normalized "$FIRST/"
  else
    for artifact in ck5200_qmk.bin ck5200_qmk.elf ck5200_qmk.map.normalized; do
      cmp "$FIRST/$artifact" "build/$artifact"
    done
  fi
done
.venv/bin/python - <<'PY'
import hashlib, pathlib
files = [pathlib.Path("build") / ("ck5200_qmk." + ext) for ext in ("bin", "elf", "map", "map.normalized")]
manifest = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files)
pathlib.Path("build/SHA256SUMS").write_text(manifest)
print(manifest, end="")
print("Two clean builds match: bin, ELF, normalized map. Raw map hash records this build.")
PY
