#!/usr/bin/env python3
"""Pad a CK-5200 application image so its staged session covers whole flash pages.

Why: the stock staging contract (device A1 handler 0x37A8) accepts images up to
0x6A00 bytes, the full application area [0x2000,0x8A00). Stock images end
mid-page, so the unknown installer's copy model decides what lands in the final
page's unwritten tail. Padding with 0xFF to a page boundary (or to 0x8A00) makes
the destination content identical under every possible copy model, because
explicit 0xFF equals erase residue. See
research_notes/CK5200 USB recovery feasibility/memory-ownership-and-oem-transition.md.

The pad never runs on the device: the application is linked at its original
extent and never reads beyond it, so appending erased-state bytes to the staged
file has no runtime effect. The padded length must still satisfy the device's
A1 contract (<= 0x6A00) and the staging fit (marker + image <= [0x8A00,0xF500)).
"""
import argparse
import hashlib
import pathlib
import sys

APP_BASE = 0x2000
APP_LIMIT = 0x8A00           # exclusive; staging base
MAX_IMAGE_SIZE = 0x6A00       # device A1 contract == APP_LIMIT - APP_BASE
STAGING_BASE = 0x8A00
STAGING_LIMIT = 0xF500        # exclusive; pages [0xF400,0xF500) are in-bounds
PAGE = 0x100
ERASED = 0xFF


def staging_fits(image_len: int) -> bool:
    """Marker (4 bytes) + image must fit the staging area, whole pages."""
    staged = 4 + image_len
    return STAGING_BASE + ((staged + PAGE - 1) // PAGE) * PAGE <= STAGING_LIMIT + 1


def pad_image(data: bytes, pad_to: int) -> bytes:
    """Pad `data` with erased-state bytes so the extent ends at linked
    address `pad_to`. `pad_to` must be page-aligned and >= image end."""
    image_end = APP_BASE + len(data)
    if pad_to < image_end:
        raise ValueError(
            f"--pad-to 0x{pad_to:x} is below the image end 0x{image_end:x}")
    if pad_to % PAGE:
        raise ValueError(f"--pad-to 0x{pad_to:x} is not {PAGE}-byte page aligned")
    if pad_to > APP_LIMIT:
        raise ValueError(
            f"--pad-to 0x{pad_to:x} exceeds the application limit 0x{APP_LIMIT:x}")
    return data + bytes([ERASED]) * (pad_to - image_end)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=pathlib.Path, help="input application .bin (base 0x2000)")
    parser.add_argument("out", type=pathlib.Path, help="output padded .bin")
    parser.add_argument(
        "--pad-to", type=lambda s: int(s, 0), default=APP_LIMIT,
        help=f"linked end address, page aligned (default 0x{APP_LIMIT:x}: full app area)")
    args = parser.parse_args()

    data = args.image.read_bytes()
    if not data:
        print("error: empty input image", file=sys.stderr)
        return 2
    if len(data) > MAX_IMAGE_SIZE:
        print(f"error: image {len(data)} exceeds the device A1 contract 0x{MAX_IMAGE_SIZE:x}",
              file=sys.stderr)
        return 2

    padded = pad_image(data, args.pad_to)
    if not staging_fits(len(padded)):
        print(f"error: padded image {len(padded)} does not fit staging", file=sys.stderr)
        return 2

    args.out.write_bytes(padded)
    tail = len(padded) - len(data)
    print(f"input  {args.image}: {len(data)} bytes, end 0x{APP_BASE + len(data):x}, "
          f"sha256 {hashlib.sha256(data).hexdigest()}")
    print(f"output {args.out}: {len(padded)} bytes, end 0x{APP_BASE + len(padded):x}, "
          f"pad {tail} x 0x{ERASED:02x}, sha256 {hashlib.sha256(padded).hexdigest()}")
    staged_end = STAGING_BASE + 4 + len(padded)
    print(f"staged session: marker+image {4 + len(padded)} bytes, staging end 0x{staged_end:x}, "
          f"last staging page 0x{(staged_end & ~0xFF):x} (limit 0x{STAGING_LIMIT:x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
