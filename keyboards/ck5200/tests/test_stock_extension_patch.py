"""Stock-extension registration redirect: patch, decode, and reversibility.

Research artifact for the programmable stock-extension feasibility milestone.
A patched image is never authorized for installation here; these tests prove
the offline hook is exact and reversible against the pinned V122 image.
See keyboards/ck5200/docs/stock-extension-feasibility.md.
"""
import importlib.util
import pathlib
import struct

import pytest

ROOT = pathlib.Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location(
    "stock_extension_patch", ROOT / "tools/stock_extension_patch.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

IMAGE = pathlib.Path(__file__).parents[3] / \
    "ios/ClicksInspector/Resources/iKeyboard_CK-5200_V122_120.bin"


def riscv_lui_imm(word: int) -> int:
    """Return the 20-bit immediate of a LUI (rd = imm<<12)."""
    assert word & 0x7F == 0x37, hex(word)
    rd = (word >> 7) & 0x1F
    imm = word >> 12
    return rd, imm


def riscv_addi_imm(word: int):
    """Return (rd, rs1, imm) of an ADDI with sign handling."""
    assert word & 0x7F == 0x13, hex(word)
    rd = (word >> 7) & 0x1F
    rs1 = (word >> 15) & 0x1F
    imm = word >> 20
    if imm & 0x800:
        imm -= 0x1000
    return rd, rs1, imm


def test_patched_image_is_hash_gated(tmp_path, capsys):
    data = IMAGE.read_bytes()
    out = tmp_path / "patched.bin"
    manifest = tmp_path / "manifest.json"
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["stock_extension_patch.py", str(IMAGE), str(out), str(manifest)]
        rc = m.main()
    finally:
        sys.argv = old_argv
    assert rc == 0
    patched = out.read_bytes()
    assert m.hashlib.sha256(patched).hexdigest() != m.IMAGE_SHA256
    # Everything outside the two registration words is byte-identical.
    assert patched[:0xA3A] == data[:0xA3A]
    assert patched[0xA3E:0xA42] == data[0xA3E:0xA42]
    assert patched[0xA46:] == data[0xA46:]
    assert struct.unpack_from("<I", patched, 0xA3A)[0] != \
        struct.unpack_from("<I", data, 0xA3A)[0]
    assert struct.unpack_from("<I", patched, 0xA42)[0] != \
        struct.unpack_from("<I", data, 0xA42)[0]


def test_patched_words_build_the_extension_address(tmp_path):
    data = IMAGE.read_bytes()
    out = tmp_path / "patched.bin"
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["stock_extension_patch.py", str(IMAGE), str(out),
                    str(tmp_path / "manifest.json")]
        m.main()
    finally:
        sys.argv = old_argv
    patched = out.read_bytes()
    lui = struct.unpack_from("<I", patched, 0xA3A)[0]
    addi = struct.unpack_from("<I", patched, 0xA42)[0]
    rd, imm = riscv_lui_imm(lui)
    assert (rd, imm) == (0xC, 0x7)          # lui a2, 0x7
    rd2, rs1, imm2 = riscv_addi_imm(addi)
    assert (rd2, rs1, imm2) == (0xC, 0xC, -0x700)   # addi a2, a2, -0x700 (signed 12-bit)
    assert (imm << 12) + imm2 == m.EXTENSION_BUILDER == 0x6900


def test_patch_is_bit_for_bit_reversible(tmp_path):
    data = IMAGE.read_bytes()
    patched = tmp_path / "patched.bin"
    restored = tmp_path / "restored.bin"
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["stock_extension_patch.py", str(IMAGE), str(patched),
                    str(tmp_path / "m1.json")]
        assert m.main() == 0
        sys.argv = ["stock_extension_patch.py", "--reverse", str(patched),
                    str(restored), str(tmp_path / "m2.json")]
        assert m.main() == 0
    finally:
        sys.argv = old_argv
    assert restored.read_bytes() == data
    assert m.hashlib.sha256(restored.read_bytes()).hexdigest() == m.IMAGE_SHA256


def test_original_words_are_the_documented_oem_registration():
    data = IMAGE.read_bytes()
    lui = struct.unpack_from("<I", data, 0xA3A)[0]
    addi = struct.unpack_from("<I", data, 0xA42)[0]
    assert lui == 0x00002637
    assert addi == 0x79660613
    rd, imm = riscv_lui_imm(lui)
    _, _, imm2 = riscv_addi_imm(addi)
    assert (imm << 12) + imm2 == m.OEM_BUILDER == 0x2796


def test_other_images_are_refused(tmp_path):
    wrong = bytearray(IMAGE.read_bytes())
    wrong[0] ^= 0xFF
    bad = tmp_path / "wrong.bin"
    bad.write_bytes(wrong)
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["stock_extension_patch.py", str(bad), str(tmp_path / "out.bin"),
                    str(tmp_path / "m.json")]
        with pytest.raises(SystemExit):
            m.main()
    finally:
        sys.argv = old_argv
    assert not (tmp_path / "out.bin").exists()
