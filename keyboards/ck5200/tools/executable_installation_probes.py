#!/usr/bin/env python3
"""Exact-image startup boundary and original receive-route controls, offline only.

Build the existing assurance runner first. Never use derived mutants as firmware.
"""
import argparse
import json
from pathlib import Path
import subprocess
from post_transfer_forensics import RAW_HASH, STOCK_HASH, TRANSFER_HASH, REQUESTS, REJECTION, digest, jump, replace_word

ROOT = Path(__file__).resolve().parents[3]
CK = ROOT / "keyboards/ck5200"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=CK / "build/installation-probes")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((CK / "build").resolve()):
        parser.error("Keep derived mutants inside keyboards/ck5200/build")
    output.mkdir(parents=True, exist_ok=True)
    raw = (ROOT / "ios/ClicksInspector/Resources/ck5200_extension.bin").read_bytes()
    stock = (CK / ".stock/iKeyboard_CK-5200_V122_120.bin").read_bytes()
    assert digest(raw) == RAW_HASH and digest(stock) == STOCK_HASH
    padded = raw + b"\xff" * 78
    assert digest(padded) == TRANSFER_HASH
    runner = CK / "build/stock_extension/assurance/runner"
    cases = [("stock", stock, "rejection"), ("exact-raw", raw, "capabilities"),
             ("exact-transfer", padded, "capabilities"),
             ("command-hook-disabled", replace_word(raw, 0x2218, 0x7FF000EF), "rejection"),
             ("initializer-skipped", replace_word(raw, 0x48CE, 0xAECFF0EF), "extension BSS not cleared"),
             ("extension-erased", raw[:0x4900] + b"\xff" * (len(raw) - 0x4900), "unexpected trap"),
             ("startup-sp-corrupt", replace_word(raw, 0x4864, 0x1FFFC117), "stack entered extension reservation"),
             ("dispatch-callback-cleared", raw, "no-reply"),
             ("redirect-oem", replace_word(raw, 0x715C, jump(0x715C, 0x3216)), "rejection")]
    results = []
    for name, data, expected in cases:
        image = output / f"{name}.bin"
        image.write_bytes(data)
        mode = "receive-corrupt-callback" if expected == "no-reply" else "receive-probe"
        trace_path = output / f"{name}.jsonl"
        proc = subprocess.run([str(runner), str(image), str(trace_path), mode],
                              input="".join(f"p {r}\n" for r in REQUESTS) + "q\n",
                              text=True, capture_output=True, timeout=60)
        replies = [l[2:] for l in proc.stdout.splitlines() if l.startswith("R ")]
        events = [json.loads(l) for l in trace_path.read_text().splitlines()]
        pcs = [e["pc"] for e in events if e["event"] == "instruction"]
        reached = 0x715C in pcs
        if expected in ("capabilities", "rejection", "no-reply"):
            assert proc.returncode == 0 and len(replies) == 2, (name, proc.stderr, replies)
            assert 0x4776 in pcs and 0x629E in pcs
            if expected == "capabilities":
                assert reached and all(p in pcs for p in (0x62BC, 0x2208, 0x2218, 0x62CC))
                for request, reply in zip(REQUESTS, replies):
                    r = bytes.fromhex(reply)
                    assert r[0] == len(r) == 27 and r[1:6].hex() == "0294000100"
                    assert r[6:10] == bytes.fromhex(request)[4:8] and r[12:16] == b"CKX1"
            else:
                assert replies == ([REJECTION] * 2 if expected == "rejection" else [""] * 2)
        else:
            assert proc.returncode != 0 and expected in proc.stderr and not replies, (name, proc.stderr)
        results.append({"case": name, "image_sha256": digest(data), "expected": expected,
                        "exit": proc.returncode, "stderr": proc.stderr.strip(), "responses": replies,
                        "instruction_addresses": [hex(p) for p in pcs], "extension_entry_reached": reached})
        print(f"PASS receive {name}: {expected}")
    startup = []
    for name, data in [("stock", stock), ("exact-raw", raw), ("exact-transfer", padded)]:
        trace_path = output / f"{name}-startup.jsonl"
        proc = subprocess.run([str(runner), str(output / f"{name}.bin"), str(trace_path), "startup-probe"],
                              text=True, capture_output=True, timeout=60)
        assert proc.returncode != 0 and "unsupported memory access at pc=0x3bbe value=0x40021000" in proc.stderr
        events = [json.loads(l) for l in trace_path.read_text().splitlines()]
        pcs = [e["pc"] for e in events if e["event"] == "instruction"]
        assert pcs[0] == 0x2000 and 0x3BBA in pcs
        startup.append({"case": name, "image_sha256": digest(data), "boundary": proc.stderr.strip(),
                        "instruction_count": len(pcs), "startup_hook_reached": 0x48CE in pcs,
                        "extension_init_reached": 0x70B2 in pcs, "trace_sha256": digest(trace_path.read_bytes())})
        print(f"PASS startup boundary {name}: RCC read at 0x3bbe")
    report = {"startup_gate": "BLOCKED at RCC CTLR read, not failed physical startup", "startup": startup,
              "receive_cases": results, "runner_sha256": digest(runner.read_bytes()),
              "limits": ["No fabricated RCC readiness or continuation after unsupported startup access.",
              "Receive cases are separate bounded fixtures after CRT, with synthetic USB and authentication.",
              "Registration helper executes, but genuine peripheral startup does not reach it in this model.",
              "Mutants establish possible responses, not the physical failure cause."]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
