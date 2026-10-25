#!/usr/bin/env bash
# Two clean builds; no device access. Preserve prior artifacts and both logs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build/software-only-evidence
EVIDENCE="$(mktemp -d "$ROOT/build/software-only-evidence/repro.XXXXXX")"
mkdir "$EVIDENCE/prior-artifacts" "$EVIDENCE/first"
for artifact in build/ck5200_qmk.* build/SHA256SUMS; do
  if [[ -f "$artifact" ]]; then cp -p "$artifact" "$EVIDENCE/prior-artifacts/"; fi
done

for pass in 1 2; do
  if [[ -f build/build.ninja ]]; then cmake --build build --target clean; fi
  bash scripts/build.sh 2>&1 | tee "$EVIDENCE/build-$pass.log"
  .venv/bin/python - "$ROOT" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
text = (root / "build/ck5200_qmk.map").read_text()
# Only host paths and GCC's random LTO temporary basename are normalized.
text = text.replace(str(root), "<ROOT>")
text = re.sub(r"\S*/cc[A-Za-z0-9]+(?=\.ltrans\d+\.ltrans\.o)", "<LTO>", text)
(root / "build/ck5200_qmk.map.normalized").write_text(text)
PY
  for artifact in ck5200_qmk.bin ck5200_qmk.hex ck5200_qmk.elf ck5200_qmk.map.normalized; do
    if [[ "$pass" == 1 ]]; then
      cp "build/$artifact" "$EVIDENCE/first/"
    else
      cmp "$EVIDENCE/first/$artifact" "build/$artifact"
    fi
  done
done
.venv/bin/python - <<'PY'
import hashlib, pathlib
files = [pathlib.Path("build") / ("ck5200_qmk." + ext) for ext in ("bin", "hex", "elf", "map", "map.normalized")]
manifest = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files)
pathlib.Path("build/SHA256SUMS").write_text(manifest)
print(manifest, end="")
print("Two clean builds match: BIN, HEX, ELF, normalized map. Raw map hash records this build.")
PY
cp build/SHA256SUMS "$EVIDENCE/"
echo "Build evidence: $EVIDENCE"
