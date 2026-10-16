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
for _ in range(struct.unpack_from("<I", executable, 16)[0]):
    command, size = struct.unpack_from("<II", executable, offset)
    assert size >= 8 and offset + size <= len(executable)
    if command == 0x2C:
        start, length, cryptid = struct.unpack_from("<III", executable, offset + 8)
        encryption.append({"offset": start, "size": length, "cryptid": cryptid})
    offset += size
assert len(encryption) == 1 and encryption[0]["cryptid"] == 0
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
                  "references": references,
                  "limit": "Command comparison evidence only; not full outgoing framing, ACK validation, installer execution or independent recovery."}, indent=2))
