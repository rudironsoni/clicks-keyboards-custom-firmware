#!/usr/bin/env python3
"""Re-extract the stock keymap table from the V122 disassembly.

The stock maps every matrix crossing inline in its HID builder at
0x2b1c..0x3010. This walks that code and prints one row per crossing:
base value, SYM-layer value (the `li hi; bnez a5; li lo` pairs), plain
values, and the cursor/context/consumer markers. Usage:

    python3 extract_keymap.py [v122.dis]

Compare the output against docs/STOCK_DECODE.md's keymap table and
firmware/keyboard/keymaps/diagnostic/keymap.c.
"""
import re
import sys
from pathlib import Path

HID_NAMES = {
    0x04: "a", 0x05: "b", 0x06: "c", 0x07: "d", 0x08: "e", 0x09: "f",
    0x0A: "g", 0x0B: "h", 0x0C: "i", 0x0D: "j", 0x0E: "k", 0x0F: "l",
    0x10: "m", 0x11: "n", 0x12: "o", 0x13: "p", 0x14: "q", 0x15: "r",
    0x16: "s", 0x17: "t", 0x18: "u", 0x19: "v", 0x1A: "w", 0x1B: "x",
    0x1C: "y", 0x1D: "z", 0x1E: "1", 0x1F: "2", 0x20: "3", 0x21: "4",
    0x22: "5", 0x23: "6", 0x24: "7", 0x25: "8", 0x26: "9", 0x27: "0",
    0x28: "Enter", 0x29: "Esc", 0x2A: "Bksp", 0x2B: "Tab", 0x2C: "Space",
    0x2D: "-", 0x2E: "=", 0x2F: "[", 0x30: "]", 0x33: ";", 0x34: "'",
    0x36: ",", 0x37: ".", 0x38: "/", 0x39: "CapsLk",
    0x4F: "Right", 0x50: "Left", 0x51: "Down", 0x52: "Up",
}

def load(dis_path: Path):
    ins, order = {}, []
    for line in dis_path.read_text().splitlines():
        parts = [p for p in line.split("\t") if p.strip()]
        if len(parts) < 3:
            continue
        m = re.match(r"\s*([0-9a-f]+):", parts[0])
        if not m:
            continue
        a = int(m.group(1), 16)
        ins[a] = (parts[1].strip(), parts[2].strip())
        order.append(a)
    order.sort()
    return ins, order

def imm(text: str):
    m = re.search(r"0x([0-9a-f]+)", text)
    return int(m.group(1), 16) if m else None

def main() -> None:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "v122.dis")
    ins, order = load(path)
    idx = {a: i for i, a in enumerate(order)}

    def text(a):
        return ins[a][0] + " " + ins[a][1]

    def tgt(t):
        m = re.search(r"0x([0-9a-f]+) <", t)
        return int(m.group(1), 16) if m else None

    crossings = {}
    for a in order:
        if not (0x2B1C <= a < 0x3010):
            continue
        m = re.match(r"lbu (\w+), 0x([0-5])\(a1\)$", text(a))
        if not m:
            continue
        row = int(m.group(2))
        i = idx[a] + 1
        if ins[order[i]][0] == "beqz":  # whole-row guard
            i += 1
        if ins[order[i]][0] != "andi":
            continue
        bit = imm(text(order[i]))
        i += 1
        if ins[order[i]][0] not in ("beqz", "bnez") or not bit:
            continue
        vals = {}
        j = i + 1
        for _ in range(10):
            if j >= len(order):
                break
            t2 = text(order[j])
            if re.match(r"lbu \w+, 0x[0-5]\(a1\)", t2):
                break
            if t2.startswith("li"):
                h = imm(t2)
                if "bnez" in text(order[j + 1]) and "a5" in text(order[j + 1]) \
                        and text(order[j + 2]).startswith("li"):
                    vals["hi"], vals["lo"] = h, imm(text(order[j + 2]))
                else:
                    vals["plain"] = h
                break
            if "0x0(a4)" in t2:
                vals["ctx"] = True
            if "0x120(a4)" in t2:
                vals["L120"] = True
            if "0x11e(a4)" in t2:
                vals["L11E"] = True
            if "0x11f(a4)" in t2:
                vals["L11F"] = True
            if "0x6(a2)" in t2 and text(order[j + 1]).startswith("ori"):
                vals["rpt6"] = imm(text(order[j + 1]))
            if "0x0(a2)" in t2 and text(order[j + 1]).startswith("ori"):
                vals["rpt0"] = imm(text(order[j + 1]))
            j += 1
        if not vals:
            t0 = tgt(text(order[i]))
            if t0 in ins and text(t0).startswith("li"):
                h = imm(text(t0))
                if "bnez" in text(order[idx[t0] + 1]) and "a5" in text(order[idx[t0] + 1]):
                    vals["hi"], vals["lo"] = h, imm(text(order[idx[t0] + 2]))
                else:
                    vals["plain"] = h
        crossings[(row, bit)] = vals

    def name(v):
        return HID_NAMES.get(v, f"0x{v:02x}" if v is not None else "-")

    print(f"{len(crossings)} crossings decoded")
    for (row, bit), v in sorted(crossings.items()):
        lo = name(v.get("lo"))
        hi = name(v.get("hi")) if "hi" in v else "(plain)" if "plain" in v else "-"
        base = name(v.get("plain")) if "plain" in v else lo
        extras = {k: v[k] for k in v if k in ("ctx", "L120", "L11E", "L11F", "rpt6", "rpt0")}
        print(f"r{row} b0x{bit:02x}: base={base:8s} sym={hi:8s} {extras if extras else ''}")

if __name__ == "__main__":
    main()
