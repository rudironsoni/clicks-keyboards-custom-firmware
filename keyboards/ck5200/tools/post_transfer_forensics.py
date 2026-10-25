#!/usr/bin/env python3
"""Replay saved CK-5200 evidence and bounded image faults. No device access.

First build the existing stock-extension assurance runner with
stock_extension/assurance/run.py. Mutants are offline inputs, never install images.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
CK = ROOT / "keyboards/ck5200"
RAW_HASH = "2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132"
TRANSFER_HASH = "438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2"
STOCK_HASH = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
REQUESTS = ["0c940100547becbf00000000", "0c940100a60a007000000000"]
REJECTION = "080294ff00000000"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_word(data, address, word):
    changed = bytearray(data)
    struct.pack_into("<I", changed, address - 0x2000, word)
    return bytes(changed)


def jump(address, target):
    imm = (target - address) & 0x1FFFFF
    return ((imm >> 20) << 31 | ((imm >> 1) & 1023) << 21
            | ((imm >> 11) & 1) << 20 | ((imm >> 12) & 255) << 12 | 0x6F)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", required=True, type=Path,
                        help="Existing directory containing StockUpdate.json and StockUpdateAudit.jsonl")
    parser.add_argument("--output", type=Path, default=CK / "build/post-transfer-forensics")
    parser.add_argument("--ios-executable", type=Path, help="Optional retained, unencrypted Clicks executable")
    args = parser.parse_args()
    evidence, output = args.evidence.resolve(), args.output.resolve()
    if output.is_relative_to(evidence) or evidence.is_relative_to(output):
        parser.error("Keep derived output separate from original evidence")
    originals = {name: (evidence / name).read_bytes()
                 for name in ("StockUpdate.json", "StockUpdateAudit.jsonl")}
    journal = json.loads(originals["StockUpdate.json"])
    records = [json.loads(line) for line in originals["StockUpdateAudit.jsonl"].splitlines()]
    assert sum(r["event"] == "trialStart" for r in records) == 1
    requests = [bytes.fromhex(r["requestHex"]) for r in records
                if r["event"] == "dispatch" and r.get("command") == 0xA2]
    replies = [bytes.fromhex(r["replyHex"]) for r in records
               if r["event"] == "reply" and r.get("command") == 0xA2]
    assert len(requests) == len(replies) == 848
    sent = bytearray()
    for request, reply in zip(requests, replies):
        assert request[0] == len(request) and int.from_bytes(request[2:6], "big") == len(sent)
        sent.extend(request[6:])
        assert int.from_bytes(reply, "big") == len(sent)
    raw = (ROOT / "ios/ClicksInspector/Resources/ck5200_extension.bin").read_bytes()
    stock = (CK / ".stock/iKeyboard_CK-5200_V122_120.bin").read_bytes()
    assert digest(raw) == RAW_HASH and digest(stock) == STOCK_HASH
    assert sent == raw + b"\xff" * 78 and digest(sent) == TRANSFER_HASH
    assert journal["imageSHA256"] == TRANSFER_HASH and journal["outcome"] == "sentRebootPending"
    assert journal["imageSize"] == journal["transferredBytes"] == len(sent) == 27136
    assert journal["rebootRequested"] is True
    assert [r["requestHex"] for r in records if r["event"] == "extension.request"] == REQUESTS
    assert [r["replyHex"] for r in records if r["event"] == "extension.reply"] == [REJECTION] * 2
    assert any(r["event"] == "reply" and r.get("command") == 0xA3
               and r["replyHex"] == "00006a00" for r in records)
    assert any(r["event"] == "reply" and r.get("command") == 0xA0
               and r["replyHex"] == "" for r in records)
    symbols = {line.split()[2]: int(line.split()[0], 16)
               for line in (CK / "build/stock_extension/symbols.txt").read_text().splitlines()
               if len(line.split()) == 3}
    assert symbols["ck_extension_command"] == 0x715C
    manifest = json.loads((CK / "build/stock_extension/patch_manifest.json").read_text())
    for site in manifest["sites"]:
        offset = int(site["file_offset"], 16)
        assert int.from_bytes(raw[offset:offset + 4], "little") == int(site["applied_word"], 16)
    timeline = []
    for record in records:
        if (record["event"] in ("trialStart", "commitUnknownRecorded")
                or record.get("command") in (0xA0, 0xA1, 0xA3)
                or (record.get("command") == 0xA2 and record["event"] == "reply"
                    and record["replyHex"] == "00006a00")
                or record["event"].startswith("extension.")):
            timeline.append({**record, "utc": datetime.fromtimestamp(
                record["date"] + 978307200, timezone.utc).isoformat()})
    output.mkdir(parents=True, exist_ok=True)
    runner = (CK / "build/stock_extension/assurance/runner").resolve()
    cases = [
        ("exact-padded", bytes(sent), "capabilities"),
        ("unchanged-stock", stock, "rejection"),
        ("skipped-command-hook", replace_word(raw, 0x2218, 0x7FF000EF), "rejection"),
        ("skipped-initializer", replace_word(raw, 0x48CE, 0xAECFF0EF), "BSS not cleared"),
        ("erased-extension", raw[:0x4900] + b"\xff" * (len(raw) - 0x4900), "unexpected trap"),
        ("command-redirect-oem", replace_word(raw, 0x715C, jump(0x715C, 0x3216)), "rejection"),
    ]
    results = []
    for name, image, expected in cases:
        path = output / f"{name}.bin"
        path.write_bytes(image)
        result = subprocess.run([str(runner), str(path), str(output / f"{name}.trace.jsonl"),
                                 "config-server"], input="".join(f"p {r}\n" for r in REQUESTS) + "q\n",
                                text=True, capture_output=True, timeout=60)
        responses = [line[2:] for line in result.stdout.splitlines() if line.startswith("R ")]
        if expected in ("capabilities", "rejection"):
            assert result.returncode == 0 and len(responses) == 2, result.stderr
            if expected == "rejection":
                assert responses == [REJECTION] * 2
            else:
                for request, response in zip(REQUESTS, responses):
                    reply = bytes.fromhex(response)
                    assert len(reply) == reply[0] == 27 and reply[1:6] == bytes.fromhex("0294000100")
                    assert reply[6:10] == bytes.fromhex(request)[4:8] and reply[12:16] == b"CKX1"
        else:
            assert result.returncode != 0 and expected in result.stderr and not responses
        results.append({"case": name, "image_sha256": digest(image), "expected": expected,
                        "exit": result.returncode, "responses": responses, "stderr": result.stderr})
        print(f"PASS {name}: {expected}")
    for name, data in originals.items():
        assert (evidence / name).read_bytes() == data, "Original evidence changed"
    report = {"physical_root_cause_established": False,
              "original_sha256": {name: digest(data) for name, data in originals.items()},
              "runner_sha256": digest(runner.read_bytes()), "timeline": timeline, "cases": results,
              "limits": ["No device access, flash readback, installer execution, or hardware reset observation.",
                         "Bounded runner executes CRT to SystemInit, then uses synthetic USB/authenticated state.",
                         "Skipped initializer stops at a host BSS invariant, not a measured guest reply.",
                         "Erased-extension and redirect cases are chosen mutations, not exhaustive corruption tests.",
                         "No attach/detach events exist in this update audit; their absence does not prove no reset."]}
    if args.ios_executable:
        ios = args.ios_executable.read_bytes()
        assert digest(ios) == "d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b"
        # These file offsets equal linked address minus 0x100000000 in this pinned Mach-O.
        call = int.from_bytes(ios[0x17B184:0x17B188], "little")
        assert call >> 26 == 0b100101  # ARM64 BL
        delta = call & 0x3FFFFFF
        if delta & 0x2000000:
            delta -= 0x4000000
        assert 0x17B184 + delta * 4 == 0x17A544
        assert ios[0x84DF30:0x84DF32] == bytes.fromhex("02a0")
        assert int.from_bytes(ios[0x17B188:0x17B18C], "little") == 0x52800028  # mov w8, #1
        report["ios_update_worker"] = {"executable_sha256": digest(ios),
            "a0_helper": "0x10017A544", "a0_call": "0x10017B184", "static_request": "02a0",
            "result": "The next instruction selects success without testing A0's return value.",
            "limit": "Static EAF worker trace, not execution on this keyboard or every dynamic app route."}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Saved derived evidence: {output / 'report.json'}")


if __name__ == "__main__":
    main()
