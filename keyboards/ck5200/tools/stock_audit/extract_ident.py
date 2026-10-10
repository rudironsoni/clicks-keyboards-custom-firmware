#!/usr/bin/env python3
"""Regenerate iap2_ident_data.h from the stock V122 image.

Extracts the sixteen Identify (0x1D01) parameter payloads from linked
0x677c..0x6862, byte-exact, and writes the header consumed by iap2.c.
Run from anywhere; paths are arguments:

    python3 extract_ident.py <stock.bin> <out_header>
"""
import sys
from pathlib import Path

BASE = 0x2000
S3 = 0x677C

PARAMS = [
    (0x00, 24, 0x677C),  # name
    (0x01, 8, 0x6794),   # model
    (0x02, 26, 0x679C),  # manufacturer
    (0x03, 10, 0x67B6),  # serial (image ASCII constant; stock reads runtime)
    (0x04, 6, 0x67C1),   # firmware version
    (0x05, 6, 0x67C7),   # hardware version
    (0x06, 8, 0x67CD),
    (0x07, 2, 0x67D5),
    (0x08, 1, 0x67D7),
    (0x09, 2, 0x67D8),
    (0x0A, 49, 0x67F5),  # EA blob with com.clickscompanion.protocol
    (0x0B, 17, 0x6826),  # 79Q5CGN6JK token
    (0x0C, 3, 0x67DA),   # "en"
    (0x0D, 3, 0x67DD),   # "en"
    (0x10, 21, 0x67E0),  # IAP2-X blob
    (0x17, 33, 0x6831),  # HID-X blob
    (0x22, 17, 0x6852),  # 020c3648d17f4624
]

def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    image = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])

    lines = [
        "/* Generated from stock iKeyboard_CK-5200_V122_120.bin, linked",
        " * 0x677c..0x6862. Identify (0x1D01) parameter payloads; the",
        " * decode record is docs/STOCK_DECODE.md. Param 3 (serial) uses the",
        " * image ASCII constant at 0x67b6; stock reads a runtime buffer",
        " * of the same value. Do not edit by hand: regenerate with",
        " * tools/stock_audit/extract_ident.py. */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct { uint8_t id; uint8_t len; const uint8_t *data; } iap2_ident_param_t;",
        "",
    ]
    for pid, length, link in PARAMS:
        chunk = image[link - BASE : link - BASE + length]
        arr = ", ".join(f"0x{b:02x}" for b in chunk)
        label = bytes(chunk).split(b"\0")[0].decode("ascii", "replace")
        lines.append(
            f"static const uint8_t ident_p{pid:02x}[{length}] = {{{arr}}}; /* {label} */"
        )
    lines += ["", "static const iap2_ident_param_t iap2_ident_params[] = {"]
    for pid, length, _ in PARAMS:
        lines.append(f"    {{0x{pid:02x}, {length}, ident_p{pid:02x}}},")
    lines += [
        "};",
        "#define IAP2_IDENT_PARAM_COUNT (sizeof(iap2_ident_params) / sizeof(iap2_ident_params[0]))",
        "",
    ]
    out.write_text("\n".join(lines))
    print(f"wrote {out}")

if __name__ == "__main__":
    main()
