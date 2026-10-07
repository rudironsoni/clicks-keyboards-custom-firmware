#!/usr/bin/env python3
import hashlib
import pathlib
import struct
import sys

MAX_IMAGE = 0x6A00

def main() -> int:
    p = pathlib.Path(sys.argv[1])
    data = p.read_bytes()
    if not data:
        raise SystemExit("empty image")
    if len(data) > MAX_IMAGE:
        raise SystemExit(f"image too large: 0x{len(data):x} > 0x{MAX_IMAGE:x}")
    if len(data) < 4:
        raise SystemExit("image too short for reset jump")
    insn = struct.unpack_from("<I", data, 0)[0]
    opcode = insn & 0x7F
    if opcode != 0x6F:
        raise SystemExit(f"first instruction is not a JAL reset jump: 0x{insn:08x}")
    print(f"image:  {p}")
    print(f"size:   {len(data)} / 0x{len(data):x} (limit 0x{MAX_IMAGE:x})")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")
    print(f"reset:  0x{insn:08x} (JAL)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
