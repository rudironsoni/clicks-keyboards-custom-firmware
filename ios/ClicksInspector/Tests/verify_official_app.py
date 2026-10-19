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

def relative(address):
    return address + struct.unpack("<i", at(address, 4))[0]

def string_at(address):
    return at(address, 128).split(b"\0", 1)[0].decode()

descriptor = 0x10068F7B8
assert string_at(relative(descriptor + 8)) == "Firmware"
assert struct.unpack("<II", at(descriptor + 20, 8)) == (4, 2)
fields = relative(descriptor + 16)
assert struct.unpack("<HI", at(fields + 10, 6)) == (12, 4)
field_names = [string_at(relative(fields + 16 + index * 12 + 8)) for index in range(4)]
assert field_names == ["model", "firmwareRevision", "file", "sha256sum"]
selection_fields = relative(0x10068F100 + 16)
assert string_at(relative(0x10068F100 + 8)) == "FirmwareListItem"
assert [string_at(relative(selection_fields + 16 + i * 12 + 8)) for i in range(3)] == ["id", "title", "source"]
source_fields = relative(0x10068F130 + 16)
assert string_at(relative(0x10068F130 + 8)) == "FirmwareSource"
source_cases = [string_at(relative(source_fields + 16 + i * 12 + 8)) for i in range(4)]
assert source_cases == ["bleLocal", "eafRemote", "bleRemote", "unsupportedRemote"]

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
update_calls = []
for address, target in [(0x10017B098, 0x100179BC4),
                        (0x10017B0CC, 0x100179BC4),
                        (0x10017B104, 0x100179BC4),
                        (0x10017B0DC, 0x100179E54),
                        (0x10017B0F4, 0x10017A2B4),
                        (0x10017B17C, 0x10017A2B4),
                        (0x10017B19C, 0x10017A2B4),
                        (0x10017B184, 0x10017A544),
                        (0x10017A8F0, 0x10011158C),
                        (0x1001115B4, 0x100581C14),
                        (0x100179D34, 0x10058231C),
                        (0x100181490, 0x10017ECCC),
                        (0x10017ECDC, 0x1001CB86C),
                        (0x1001CBA2C, 0x1005B2B00)]:
    instruction = struct.unpack("<I", at(address, 4))[0]
    assert instruction >> 26 == 0b100101
    displacement = instruction & 0x3FFFFFF
    if displacement & (1 << 25):
        displacement -= 1 << 26
    assert address + displacement * 4 == target
    update_calls.append({"call": hex(address), "target": hex(target)})
# After the A0 call the routine constructs success without testing its return.
assert at(0x10017B188, 12) == bytes.fromhex("28 00 80 52 e8 03 00 f9 ff 23 00 39")
print(json.dumps({"executable_sha256": hashlib.sha256(executable).hexdigest(),
                  "encryption": encryption, "comparisons": comparisons,
                  "request_constants": request_constants, "references": references,
                  "update_calls": update_calls, "a0_return_checked_by_caller": False,
                  "firmware_fields": field_names,
                  "firmware_source_cases": source_cases,
                  "limit": "Static updater evidence only. See stock-manager-readiness.md for instruction-level comparison and limits. No installer execution or independent recovery is established."}, indent=2))
