#!/usr/bin/env python3
"""Offline reconstruction of pinned Clicks 3.0.2 instructions, not app execution.

No accessory, network, extraction, installer, or journal-writing interface exists.
The model describes EAF worker behavior, not OS scheduling or physical flash.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import zipfile

IOS_HASH = "d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b"
BASE = 0x100000000
# Half-open instruction ranges. Publish hashes, not vendor machine code.
RANGES = {
    "integer-bytes": (0x111A80, 0x111FDC), "chunks": (0x11201C, 0x112324),
    "array-append": (0x4EC78, 0x4ED68), "sha256-file": (0x11158C, 0x111A80),
    "version-compare": (0x110DD4, 0x1114B8), "source-routing": (0xD7D5C, 0xD80AC),
    "source-dispatch": (0xD81C8, 0xD8540), "manifest-url": (0xE6E48, 0xE6F28),
    "A1": (0x179BC4, 0x179E54), "A2": (0x179E54, 0x17A2B4),
    "A3": (0x17A2B4, 0x17A544), "A0": (0x17A544, 0x17A740),
    "image-validation": (0x17A740, 0x17AD48), "worker": (0x17B05C, 0x17B218),
    "manifest-selection": (0x17B218, 0x17B7B8), "download": (0x17B7B8, 0x17C1DC),
    "manifest-response": (0x17C464, 0x17C680), "manifest-fetch": (0x180F80, 0x1812B4),
    "discovery": (0x1A43A0, 0x1A4928), "session-binding": (0x1A4928, 0x1A51B8),
    "disconnect": (0x1B44B4, 0x1B453C), "session-open-close": (0x1CBD64, 0x1CC0F8),
    "writer": (0x1CB86C, 0x1CBD64), "input": (0x1CC38C, 0x1CCDE0),
    "reply-store": (0x1CCE68, 0x1CCEE0), "notifications": (0x1CD438, 0x1CD7CC),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def inventory_archive(path):
    signatures = [(b"\x1f\x8b", "gzip"), (b"PK\x03\x04", "zip"),
                  (b"\xfd7zXZ\x00", "xz"), (b"\x04\x22\x4d\x18", "lz4"),
                  (b"\x7fELF", "ELF"), (b"\xcf\xfa\xed\xfe", "Mach-O"), (b"\xca\xfe\xba\xbe", "class-or-fat-Mach-O")]
    rows = []
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            if member.is_dir():
                continue
            data = archive.read(member)
            kind = next((name for magic, name in signatures if data.startswith(magic)), "other")
            row = {"path": member.filename, "bytes": len(data), "sha256": sha(data), "magic_kind": kind}
            if kind == "gzip":
                expanded = gzip.decompress(data)
                row.update(expanded_bytes=len(expanded), expanded_sha256=sha(expanded))
            rows.append(row)
    return rows


def scalar(value):
    # 0x100111A80: radix 16, odd-digit zero prefix, consecutive hex pairs.
    text = f"{value:X}"
    return bytes.fromhex(("0" if len(text) % 2 else "") + text)


def sized(opcode, length):
    return bytes([6, opcode]) + scalar(length).rjust(4, b"\0")


def chunks(image):
    return [image[i:i + 32] for i in range(0, len(image), 32)]


def a2(chunk, offset_bytes):
    return scalar(len(chunk) + 6) + b"\xa2" + offset_bytes + chunk


def matches(reply, op):
    # No declared-frame-length, status-byte, capacity or committed-size check.
    return reply is not None and len(reply) > 2 and reply[1:3] == bytes([2, op])


def version(value):
    # Inspected decimal-component paths, absent or unparsable parts become zero.
    def integer(part):
        try:
            return int(part, 10)
        except ValueError:
            return 0
    return [integer(p) for p in value.split(".")]


def newer(candidate, current):
    a, b = version(candidate), version(current)
    n = max(len(a), len(b))
    return a + [0] * (n - len(a)) > b + [0] * (n - len(b))


def select(items, model, current):
    # 0x10017B538..0x10017B7B0: first matching newer entry, then return.
    return next((i for i in items if i["model"] == model and newer(i["firmwareRevision"], current)), None)


def prepare(image, expected_hash):
    if sha(image) != expected_hash:
        raise ValueError("hash-mismatch")
    return image


def worker(image, fault=None):
    """Reconstructed worker with explicit synthetic replies and write results."""
    trace = []
    offset = bytes(4)
    for op, packet in [(0xA1, sized(0xA1, len(image))),
                       *[(0xA2, c) for c in chunks(image)],
                       (0xA3, sized(0xA3, len(image))), (0xA0, b"\x02\xa0")]:
        if op == 0xA2:
            packet = a2(packet, offset)
        payload = (int.from_bytes(offset, "big") + len(packet) - 6).to_bytes(4, "big") if op == 0xA2 else len(image).to_bytes(4, "big")
        reply = bytes([4 if op == 0xA0 else 8, 2, op, 0]) + (b"" if op == 0xA0 else payload)
        write_count, wait_seconds = len(packet), 0
        if fault and fault[0] == op:
            kind = fault[1]
            if kind in ("lost", "disconnect"):
                reply, wait_seconds = None, 3
            elif kind == "reject":
                reply = bytes([4, 2, op, 0xFF]) + (bytes(4) if op == 0xA2 else b"")
            elif kind == "wrong-op":
                reply = b"\x04\x02\xff\xff"
            elif kind == "short-write":
                write_count = 1  # The writer ignores the returned count.
            elif kind == "partial":
                reply = bytes([8, 2, op, 0]) + (1).to_bytes(4, "big")
            elif kind == "fragment":
                reply = reply[:3]  # One stream read, not a reassembled reply.
        trace.append({"request": packet.hex(), "write_return": write_count,
                      "reply": None if reply is None else reply.hex(), "wait_seconds": wait_seconds})
        if op == 0xA0:
            return {"state": "reported-success", "trace": trace}
        if not matches(reply, op):
            return {"state": {0xA1: "error-2", 0xA2: "error-3", 0xA3: "error-4"}[op], "trace": trace}
        if op == 0xA2:
            if len(reply) < 8:
                return {"state": "Data-range-precondition", "trace": trace}
            offset = reply[4:8]  # Next source chunk still advances independently.
    raise AssertionError("worker did not finish")


def behavioral_cases():
    image = bytes(range(65))
    specs = [("normal", None, "reported-success")]
    for op in (0xA1, 0xA2, 0xA3, 0xA0):
        for kind in ("lost", "reject", "wrong-op"):
            expected = "reported-success" if op == 0xA0 or kind == "reject" else {0xA1: "error-2", 0xA2: "error-3", 0xA3: "error-4"}[op]
            specs.append((f"{op:02x}-{kind}", (op, kind), expected))
    specs += [("a2-short-write-with-reply", (0xA2, "short-write"), "reported-success"),
              ("a2-partial-progress", (0xA2, "partial"), "reported-success"),
              ("a2-fragmented-reply", (0xA2, "fragment"), "Data-range-precondition"),
              ("disconnect-staging", (0xA2, "disconnect"), "error-3"),
              ("disconnect-after-commit", (0xA0, "disconnect"), "reported-success")]
    results = []
    for name, fault, expected in specs:
        result = worker(image, fault)
        assert result["state"] == expected, (name, result)
        results.append({"case": name, **result})
    assert sized(0xA1, 0x6A00).hex() == "06a100006a00"
    assert sized(0xA3, 27058).hex() == "06a3000069b2"
    assert a2(bytes(range(3)), bytes(4)).hex() == "09a200000000000102"
    item = {"model": "CK-5200", "firmwareRevision": "1.2.2"}
    assert select([item], "CK-5200", "1.2.1") == item
    assert select([item], "CK-5300", "1.2.1") is None
    assert select([item], "CK-5200", "1.2.2") is None  # Reconnect unchanged.
    assert select([item, {**item, "firmwareRevision": "1.2.3"}], "CK-5200", "1.2.1") == item
    assert prepare(image, sha(image)) == image
    try:
        prepare(image, sha(image + b"\xff"))
    except ValueError as error:
        assert str(error) == "hash-mismatch"
    else:
        raise AssertionError("Invalid expected hash accepted")
    # No hardwareRevision or size argument exists in the traced selector/validator.
    assert sized(0xA1, 0x6A01).hex() == "06a100006a01"  # Device acceptance is a separate gate.
    return results


def verify_executable(executable):
    assert sha(executable) == IOS_HASH
    for offset, data in [(0x84DFF0, "06a1"), (0x84DFC0, "a2"), (0x84DF90, "00000000"),
                         (0x84DF60, "06a3"), (0x84DF30, "02a0")]:
        assert executable[offset:offset + len(bytes.fromhex(data))].hex() == data
    def bl(site, target):
        word = struct.unpack_from("<I", executable, site)[0]
        delta = word & 0x3FFFFFF
        if delta & 0x2000000:
            delta -= 0x4000000
        assert word >> 26 == 0b100101 and site + delta * 4 == target
    for site, target in [(0x179C00, 0x111A80), (0x179EDC, 0x11201C), (0x17A2F0, 0x111A80),
                         (0x17B098, 0x179BC4), (0x17B0DC, 0x179E54), (0x17B184, 0x17A544),
                         (0x1CBA2C, 0x5B2B00), (0x17A8F0, 0x11158C)]:
        bl(site, target)
    assert struct.unpack_from("<I", executable, 0x17B188)[0] == 0x52800028
    return {name: {"start": hex(BASE + lo), "end": hex(BASE + hi), "sha256": sha(executable[lo:hi])}
            for name, (lo, hi) in RANGES.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ipa", type=Path, required=True)
    parser.add_argument("--apk", type=Path, help="Optional retained original Android package inventory")
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output, evidence = args.output.resolve(), args.evidence.resolve()
    if (output.is_relative_to(evidence) or evidence.is_relative_to(output)
            or any(p.resolve().is_relative_to(output) for p in (args.ipa, args.apk) if p)):
        parser.error("Output must not overlap primary inputs")
    audit_path = evidence / "StockUpdateAudit.jsonl"
    original = audit_path.read_bytes()
    with zipfile.ZipFile(args.ipa) as archive:
        executable = archive.read("Payload/Clicks.app/Clicks")
        anchors = verify_executable(executable)
    inventory = inventory_archive(args.ipa)
    records = [json.loads(line) for line in original.splitlines()]
    sent = b"".join(bytes.fromhex(r["requestHex"])[6:] for r in records if r["event"] == "dispatch" and r.get("command") == 0xA2)
    assert len(sent) == 27136 and sha(sent) == "438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2"
    expected = {0xA1: [sized(0xA1, len(sent))], 0xA2: [a2(c, (i * 32).to_bytes(4, "big")) for i, c in enumerate(chunks(sent))],
                0xA3: [sized(0xA3, len(sent))], 0xA0: [b"\x02\xa0"]}
    for op, packets in expected.items():
        captured = [bytes.fromhex(r["requestHex"]) for r in records if r["event"] == "dispatch" and r.get("command") == op]
        assert captured == packets, hex(op)
    assert audit_path.read_bytes() == original
    cases = behavioral_cases()
    output.mkdir(parents=True, exist_ok=True)
    (output / "ipa-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    if args.apk:
        assert sha(args.apk.read_bytes()) == "fc60df6c69f0fb142892b9a8b531c60fb439b8d7e37bbde01886eea296499740"
        (output / "apk-inventory.json").write_text(json.dumps(inventory_archive(args.apk), indent=2) + "\n")
    report = {"kind": "instruction-supported reconstruction, NOT iOS execution", "executable_sha256": IOS_HASH,
              "ipa_sha256": sha(args.ipa.read_bytes()), "audit_sha256": sha(original), "anchors": anchors,
              "physical_frame_matches": {f"{op:02x}": len(frames) for op, frames in expected.items()},
              "selection_cases": ["matching-newer", "wrong-model", "unchanged-version", "first-match-order"],
              "validation_cases": ["valid-hash-raw-bytes", "invalid-hash", "no-host-length-limit"],
              "cases": cases, "limits": ["OS stream scheduling, races and disconnect timing are not executed.",
              "Synthetic replies do not establish staging or boot-stage behavior."]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(cases)} worker cases, 7 selection/validation assertions, 851 captured frames, {len(inventory)} IPA members")


if __name__ == "__main__":
    main()
