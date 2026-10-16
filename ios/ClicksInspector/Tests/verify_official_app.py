#!/usr/bin/env python3
"""Read-only, standard-library checks of the supplied decrypted 3.0.2 IPA.

Print only hashes, protocol references and instruction metadata. No extraction,
execution, network access, archive metadata or proprietary payload publication.
"""
import hashlib
import json
import struct
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as archive:
    executable = archive.read("Payload/Clicks.app/Clicks")
assert hashlib.sha256(executable).hexdigest() == "d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b"
assert struct.unpack_from("<I", executable)[0] == 0xFEEDFACF
offset = 32
encryption = []
segments = []
for _ in range(struct.unpack_from("<I", executable, 16)[0]):
    command, size = struct.unpack_from("<II", executable, offset)
    assert size >= 8 and offset + size <= len(executable)
    if command == 0x2C:
        start, length, cryptid = struct.unpack_from("<III", executable, offset + 8)
        encryption.append({"offset": start, "size": length, "cryptid": cryptid})
    if command == 0x19:
        segments.append(struct.unpack_from("<QQQQ", executable, offset + 24))
    offset += size
assert len(encryption) == 1 and encryption[0]["cryptid"] == 0

def at(address, size):
    vm, _, file_offset, file_size = next(s for s in segments if s[0] <= address < s[0] + s[3])
    offset = file_offset + address - vm
    assert address - vm + size <= file_size
    return executable[offset:offset + size]

# Static Swift byte-array headers, referenced by the four updater wrappers.
request_constants = []
for address, value in [(0x10084DFD0, bytes.fromhex("06 a1")),
                       (0x10084DFA0, bytes.fromhex("a2")),
                       (0x10084DF70, bytes(4)),
                       (0x10084DF40, bytes.fromhex("06 a3")),
                       (0x10084DF10, bytes.fromhex("02 a0"))]:
    assert struct.unpack("<Q", at(address + 16, 8))[0] == len(value)
    assert at(address + 32, len(value)) == value
    request_constants.append({"address": hex(address), "bytes": value.hex(" ")})
comparisons = []
for address, word, opcode in [(0x100179DD4, 0x710286FF, 0xA1),
                              (0x10017A0DC, 0x7102891F, 0xA2),
                              (0x10017A4C4, 0x71028EFF, 0xA3),
                              (0x10017A6C0, 0x710282FF, 0xA0)]:
    actual = struct.unpack_from("<I", executable, address - 0x100000000)[0]
    assert actual == word and ((actual >> 10) & 0xFFF) == opcode
    comparisons.append({"address": hex(address), "word": hex(word), "compared_opcode": hex(opcode)})
references = ["https://xinyi1.clicks.tech/firmwares.json", "com.clickscompanion.protocol"]
for value in references:
    assert value.encode() in executable
print(json.dumps({"executable_sha256": hashlib.sha256(executable).hexdigest(),
                  "encryption": encryption, "comparisons": comparisons,
                  "request_constants": request_constants, "references": references,
                  "limit": "Static updater evidence only. See stock-manager-readiness.md for instruction-level comparison and limits. No installer execution or independent recovery is established."}, indent=2))
