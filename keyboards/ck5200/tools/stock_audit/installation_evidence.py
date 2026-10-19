"""Read-only, pinned-byte evidence for the CK-5200 installation investigation.

This decodes startup address calculations, not the OEM installer. It has no
device transport and makes no claim about unread destination flash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


PINS = {
    "V121": (18572, "1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa"),
    "V122": (18648, "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"),
}
QMK_PIN = "969e6d71a177119e19dd0a93fffee4cfc63f2c1b46c01a9ccf01d56ac353694e"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def word(data, address):
    return struct.unpack_from("<I", data, address - 0x2000)[0]


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def reset_target(data):
    ins = word(data, 0x2000)
    require(ins & 0xFFF == 0x06F, "expected JAL x0 reset instruction")
    imm = ((ins >> 31) << 20 | ((ins >> 12) & 255) << 12 |
           ((ins >> 20) & 1) << 11 | ((ins >> 21) & 1023) << 1)
    return 0x2000 + signed(imm, 21)


def pc_address(data, pc, register):
    upper, lower = word(data, pc), word(data, pc + 4)
    require(upper & 0xFFF == (register << 7 | 0x17), "expected AUIPC")
    require(lower & 0xFFFFF == (register << 15 | register << 7 | 0x13),
            "expected ADDI of the same register")
    return (pc + signed(upper & 0xFFFFF000, 32) + signed(lower >> 20, 12)) & 0xFFFFFFFF


def stock_evidence(directory, revision):
    path = directory / f"iKeyboard_CK-5200_{revision}_120.bin"
    data = path.read_bytes()
    size, digest = PINS[revision]
    require(len(data) == size and hashlib.sha256(data).hexdigest() == digest,
            f"{revision}: pinned size/SHA-256 mismatch")
    reset = reset_target(data)
    gp = pc_address(data, reset, 3)
    source = pc_address(data, reset + 16, 10)
    destination = pc_address(data, reset + 24, 11)
    limit_ins = word(data, reset + 32)
    require(limit_ins & 0xFFFFF == (3 << 15 | 12 << 7 | 0x13), "expected ADDI a2,gp")
    limit = gp + signed(limit_ins >> 20, 12)
    copy_end = source + limit - destination
    require(copy_end == 0x2000 + len(data), "startup copy does not end at image end")
    return {"file": path.name, "bytes": size, "sha256": digest,
            "file_extent": [0, size], "linked_extent": [0x2000, 0x2000 + size],
            "reset": reset, "gp": gp, "copy_source": [source, copy_end],
            "copy_destination": [destination, limit],
            "last_page": [(0x2000 + size) & ~255, (0x2000 + size + 255) & ~255]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock_directory", type=Path)
    parser.add_argument("qmk_bin", type=Path)
    args = parser.parse_args()
    stocks = [stock_evidence(args.stock_directory, rev) for rev in PINS]
    v122 = (args.stock_directory / "iKeyboard_CK-5200_V122_120.bin").read_bytes()
    require(v122[0x17A8:0x17B2].hex() == "1d67130707a0637af702",
            "V122 A1 capacity instructions changed")
    require(v122[0x1A24:0x1A34].hex() == "b707efbe37e700e0938707083cc701a0",
            "V122 deferred system-reset instructions changed")
    qmk = args.qmk_bin.read_bytes()
    require(hashlib.sha256(qmk).hexdigest() == QMK_PIN, "QMK v1 SHA-256 mismatch")
    start, stop = 0x68D8, 0x6900
    result = {
        "scope": "Static binary evidence. No OEM installer or physical device executes.",
        "stock": stocks,
        "growth": stocks[1]["bytes"] - stocks[0]["bytes"],
        "v122_payload_beyond_v121_extent": [0x688C, 0x68D8],
        "a1_capacity": {"instruction_range": [0x37A8, 0x37B2], "bytes": 0x6A00,
                        "implied_app_extent": [0x2000, 0x8A00],
                        "limit": "Accepted staging length, not proof of installer acceptance."},
        "a0_deferred_reset": {"handler": 0x3954, "foreground_call": 0x238E,
                              "instruction_range": [0x3A24, 0x3A34],
                              "register": 0xE000E048, "value": 0xBEEF0080,
                              "limit": "System reset request. Post-reset OEM code is absent."},
        "questioned_tail": {"linked_extent": [start, stop],
                            "v122_file_offsets_absent": [start - 0x2000, stop - 0x2000],
                            "qmk_file_offsets_present": [start - 0x2000, stop - 0x2000],
                            "qmk_bytes_hex": qmk[start - 0x2000:stop - 0x2000].hex(),
                            "destination_contents": "unknown, not present in either stock file"},
        "qmk": {"bytes": len(qmk), "sha256": QMK_PIN, "extent": [0x2000, 0x2000 + len(qmk)],
                "capacity_free": 0x6A00 - len(qmk),
                "whole_page_reservation_naive_shortfall": len(qmk) + 256 - 0x6A00},
        "conclusion": "Image end is not a fixed application boundary. Tail ownership remains unproven.",
    }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
