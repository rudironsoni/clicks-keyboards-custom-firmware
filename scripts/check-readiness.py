#!/usr/bin/env python3
"""Reject specific retired operating claims in tracked docs and iOS UI copy."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
patterns = [
    r"always restore stock",
    r"Mac path restores stock in about two minutes",
    r"Mac restore path is always available as backup",
    r"ready for (?:its |the )?first flash",
    r"recommended first flash",
    r"Durable immediately",
    r"persists in the keyboard's flash immediately",
    r"wear.leveled EEPROM.*0x0800F800",
    r"sets and persists the byte through the wear.leveled EEPROM",
    r"stock.restore safety net",
]
for phrase in ["always restore stock", "Durable immediately", "ready for its first flash"]:
    assert any(re.search(p, phrase, re.I) for p in patterns)
errors = []
paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=ROOT, text=True).splitlines()
for name in sorted(set(paths)):
    if not (name.endswith(".md") or name.startswith("ios/ClicksInspector/Sources/") and name.endswith(".swift")):
        continue
    for number, line in enumerate((ROOT / name).read_text().splitlines(), 1):
        if any(re.search(p, line, re.I) for p in patterns):
            # Historical review quotations explicitly refuting the claim are evidence, not instructions.
            if "/docs/reviews/" in name and re.search(r"refuted|contradicted|which is false|does not survive", line, re.I):
                continue
            errors.append(f"{name}:{number}: obsolete assertion: {line}")
readiness = (ROOT / "keyboards/ck5200/docs/flash-readiness.md").read_text()
for required in ["authoritative readiness", "UNMET", "UNVERIFIED", "NO-GO", "Diagnostics-only", "transient RAM"]:
    if required.lower() not in readiness.lower(): errors.append(f"Readiness record missing: {required}")
for name in ["HANDOFF.md", "README.md", "keyboards/ck5200/STATUS.md", "keyboards/ck5200/docs/FLASHING.md", "keyboards/ck5200/docs/BUILD_AND_FLASH.md"]:
    if "flash-readiness.md" not in (ROOT / name).read_text(): errors.append(f"{name}: missing authoritative readiness link")
if errors:
    raise SystemExit("\n".join(errors))
print("PASS readiness consistency: retired recovery, flash authorization, and persistence assertions absent")
