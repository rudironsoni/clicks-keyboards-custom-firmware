#!/usr/bin/env python3
"""Create a same-size, one-byte stock-image probe.

This is intentionally separate from the QMK image. It can be used to answer
one question before a QMK first flash: does the reboot-time installer accept
an image whose payload differs from the manifest hash?

The script only patches the five-byte ASCII stock version string 1.2.2 to
1.2.P. It never writes USB.
"""
import argparse
import hashlib
from pathlib import Path

EXPECTED = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
OLD = b"1.2.2"
NEW = b"1.2.P"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stock_v122")
    ap.add_argument("output")
    args = ap.parse_args()
    src = Path(args.stock_v122).read_bytes()
    if hashlib.sha256(src).hexdigest() != EXPECTED:
        raise SystemExit("input is not the verified CK-5200 1.2.2 image")
    if src.count(OLD) != 1:
        raise SystemExit("expected exactly one 1.2.2 string")
    out = src.replace(OLD, NEW)
    if len(out) != len(src):
        raise SystemExit("internal error: size changed")
    Path(args.output).write_bytes(out)
    print(f"wrote {args.output}")
    print(f"size   {len(out)}")
    print(f"sha256 {hashlib.sha256(out).hexdigest()}")
    print("diff bytes:", sum(a != b for a, b in zip(src, out)))

if __name__ == "__main__":
    main()
