"""Page-padded staging images: contract, bounds, and pad semantics.

See research_notes/CK5200 USB recovery feasibility/memory-ownership-and-oem-transition.md
for the evidence: stock images end mid-page, every OEM update erases the final
page, and no stock code reads the tail. Padding with erased-state bytes makes
the staged session cover whole pages, so every possible installer copy model
produces the same destination content.
"""
from pathlib import Path
import importlib.util

import pytest

ROOT = Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location("pad_staging_image", ROOT / "tools/pad_staging_image.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

APP_BASE = m.APP_BASE
APP_LIMIT = m.APP_LIMIT
MAX_IMAGE_SIZE = m.MAX_IMAGE_SIZE
PAGE = m.PAGE


def test_full_area_pad_matches_device_contract():
    """The default pad reproduces exactly the extent the device A1 handler
    accepts: [0x2000,0x8A00) (V122 0x37A8 checks len <= 0x6A00)."""
    image = bytes(range(256)) * 40 + b"\x00" * 100   # 10,300 bytes, mid-page end
    padded = m.pad_image(image, m.APP_LIMIT)
    assert len(padded) == MAX_IMAGE_SIZE == APP_LIMIT - APP_BASE
    assert padded[:len(image)] == image
    assert set(padded[len(image):]) == {0xFF}


def test_page_boundary_pad():
    image = b"\xAB" * 0x100 + b"\x11\x22\x33"       # ends at linked 0x2303
    padded = m.pad_image(image, 0x2400)
    assert len(padded) == 0x400
    assert APP_BASE + len(padded) == 0x2400
    assert padded[0x100:0x103] == b"\x11\x22\x33"
    assert set(padded[0x103:]) == {0xFF}


def test_pad_never_changes_original_bytes():
    image = bytes((i * 7) & 0xFF for i in range(0x3F0))
    padded = m.pad_image(image, 0x2400)
    assert padded[:len(image)] == image
    assert padded.endswith(b"\xff")


def test_staging_fits_full_area():
    """Staging [0x8A00,0xF500) holds marker + image on whole pages, so images
    up to 27,388 bytes fit; the device A1 contract (0x6A00) is the binding
    limit, and every contract-legal length fits."""
    assert m.staging_fits(MAX_IMAGE_SIZE)
    assert m.staging_fits(0xF500 - m.STAGING_BASE - 4)      # 27,388: ends exactly at 0xF500
    assert not m.staging_fits(0xF500 - m.STAGING_BASE - 4 + 1)  # needs one more page


def test_rejects_pad_below_image_end_and_unaligned():
    image = b"\x00" * 0x1000
    with pytest.raises(ValueError):
        m.pad_image(image, 0x2000 + len(image) - 1)
    with pytest.raises(ValueError):
        m.pad_image(image, 0x2000 + len(image) + 0x10)   # not page aligned


def test_rejects_pad_above_app_limit():
    image = b"\x00" * 0x10
    with pytest.raises(ValueError):
        m.pad_image(image, APP_LIMIT + PAGE)


def test_cli_rejects_oversize_image(tmp_path, capsys):
    oversize = bytes([0xFF]) * (MAX_IMAGE_SIZE + 1)
    src = tmp_path / "big.bin"
    src.write_bytes(oversize)
    out = tmp_path / "out.bin"
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["pad_staging_image.py", str(src), str(out)]
        rc = m.main()
    finally:
        sys.argv = old_argv
    assert rc == 2
    assert not out.exists()
    assert "A1 contract" in capsys.readouterr().err


def test_cli_pads_real_candidate_extent(tmp_path, capsys):
    """The actual QMK v1 artifact (12cfe69, CI run 39180108400): 26,940 bytes,
    extent [0x2000,0x893C). The padded session must end exactly at 0x8A00 and
    inside the last legal staging page."""
    image = bytes([0x00]) * (0x893C - APP_BASE)
    src = tmp_path / "candidate.bin"
    src.write_bytes(image)
    out = tmp_path / "padded.bin"
    import sys
    old_argv = sys.argv
    try:
        sys.argv = ["pad_staging_image.py", str(src), str(out)]
        rc = m.main()
    finally:
        sys.argv = old_argv
    assert rc == 0
    padded = out.read_bytes()
    assert len(padded) == MAX_IMAGE_SIZE
    assert APP_BASE + len(padded) == APP_LIMIT
    assert padded[0x693C:] == b"\xff" * (MAX_IMAGE_SIZE - 0x693C)   # 196-byte pad
    staged_end = m.STAGING_BASE + 4 + len(padded)
    assert (staged_end & ~0xFF) == 0xF400       # last staging page in bounds
    assert staged_end <= m.STAGING_LIMIT + 1
