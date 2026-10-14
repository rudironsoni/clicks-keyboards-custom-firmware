#!/usr/bin/env python3
"""Run current regressions against the published review target, without editing the checkout."""
from pathlib import Path
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "ios/ClicksInspector"
BASE = "8bde3975ad72579ce3406e9236de758230b9851e"
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix="ck5200-red-") as directory:
    temp = Path(directory)
    def source(name):
        path = temp / (name + ".swift")
        path.write_bytes(subprocess.check_output(["git", "show", f"{BASE}:ios/ClicksInspector/Sources/{name}.swift"], cwd=ROOT))
        return str(path)

    evidence = [source(n) for n in ["StockReadCommand", "StockEvidence", "StockEvidencePrivacy"]]
    controller = [source(n) for n in ["KeycodeNames", "FactoryLayout", "KeyboardSelfTest", "CustomKeyboardProtocol", "CustomFirmwareController"]]
    cases = [
        ("version", evidence[:1], "StockVersionRegression", [], "mismatched version was validated"),
        ("session", evidence, "StockEvidenceTests", ["--session-reset"], "new session kept old condition"),
        ("diagnostics", controller, "DiagnosticsOnlyTests", [str(APP / "Resources/iKeyboard_CK-5200_V122_120.bin")], "diagnostics dispatched firmware write"),
    ]
    for name, sources, test, args, expected in cases:
        binary = temp / name
        subprocess.run(["xcrun", "swiftc", *sources, str(APP / f"Tests/{test}.swift"), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary), *args], capture_output=True, text=True)
        print(f"BASE {BASE}: {name}, exit={result.returncode}", flush=True)
        print(result.stdout + result.stderr, flush=True)
        if result.returncode == 0 or expected not in result.stderr:
            raise SystemExit(f"Expected behavioral failure missing: {expected}")
print("PASS red-first reproduction: all three behavioral failures observed on review target")
