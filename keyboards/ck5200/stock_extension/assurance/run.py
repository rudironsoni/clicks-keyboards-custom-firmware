#!/usr/bin/env python3
"""Bounded V122 scanner execution. Separate from the exact-QMK gate."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "build/stock_extension/assurance"
UPSTREAM = ROOT / "external/chgame_emulator"
PIN = "bb183d7863961b5f53dab3c8d3d3c7a9dc4b1a48"
STOCK_HASH = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"


def command(args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, text=True,
                          timeout=120, **kwargs)


def main():
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=UPSTREAM, text=True).strip()
    assert revision == PIN, "CHGame pin mismatch"
    assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=UPSTREAM), "dirty CHGame dependency"
    stock = ROOT / ".stock/iKeyboard_CK-5200_V122_120.bin"
    assert hashlib.sha256(stock.read_bytes()).hexdigest() == STOCK_HASH
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / "src"
    source.mkdir(exist_ok=True)
    names = ["rv32", "bus", "machine", "st7735", "sdspi", "sdcard"]
    for name in [*(n + ".c" for n in names), "machine.h", "sdspi.h", "sdcard.h"]:
        shutil.copyfile(UPSTREAM / "src" / name, source / name)
    command(["patch", "--batch", "--fuzz=0", "-p1", "-i", ROOT / "assurance/upstream.patch"], cwd=source)
    symbols = {line.split()[2]: int(line.split()[0], 16)
               for line in (OUT.parent / "symbols.txt").read_text().splitlines() if len(line.split()) == 3}
    toolchain = sorted(ROOT.glob(".toolchains/xpack-riscv-none-elf-gcc-*/bin"))[0]
    elf = OUT.parent / "ck5200_extension.elf"
    # Do not trust an old symbols.txt or a BIN from a different ELF.
    actual = subprocess.check_output([str(toolchain / "riscv-none-elf-nm"), "-n", str(elf)], text=True)
    assert actual == (OUT.parent / "symbols.txt").read_text(), "stale symbol evidence"
    elf_bin = OUT / "elf-extracted.bin"
    command([toolchain / "riscv-none-elf-objcopy", "-O", "binary", elf, elf_bin])
    assert elf_bin.read_bytes() == (OUT.parent / "ck5200_extension.bin").read_bytes(), "BIN/ELF mismatch"
    command([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
             "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             f"-DCK_BSS_START={symbols['__extension_bss_start']}",
             f"-DCK_BSS_END={symbols['__extension_bss_end']}",
             "-I", source, "-I", HERE, "-include", HERE / "gate.h",
             *(source / (n + ".c") for n in names), HERE / "runner.c", "-o", OUT / "runner"])
    report = {"profile": "V122-bounded-scanner", "passed": False, "chgame_pin": PIN,
              "limitations": ["Synthetic GPIO closures, settings, and ready EP1 host.",
                              "CRT stops before SystemInit. Scanner initializer executes separately.",
                              "Timer driver executes through the genuine ISR JAL target, not IRQ entry/return.",
                              "No enumeration, iAP2 session, physical USB, install, or recovery proof."],
              "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [HERE / "run.py", HERE / "runner.c", HERE / "gate.h",
                                          ROOT / "assurance/upstream.patch"]},
              "cases": []}
    try:
        extension = OUT.parent / "ck5200_extension.bin"
        mutant = OUT / "no-timer-hook.bin"
        changed = bytearray(extension.read_bytes())
        struct.pack_into("<I", changed, 0x3AEE - 0x2000, 0xFECFE0EF)
        mutant.write_bytes(changed)
        default_paths = []
        for name, image in [("stock", stock), ("extension", extension),
                            ("remap", extension), ("remap-delayed-host", extension),
                            ("remap-no-timer-negative", mutant)]:
            trace = OUT / f"{name}.trace.jsonl"
            remap = name.startswith("remap")
            config = [hex(symbols[s]) for s in ("ck_remapping_enabled", "ck_extension_keymap")] if remap else []
            if name == "remap-delayed-host":
                config.append("delayed-host")
            command([OUT / "runner", image, trace, *config], env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            packets = [e["hex"] for e in events if e["event"] == "EP1-host-consume"]
            assert "0000140000000000" in packets, "Q press missing"
            press = packets.index("0000140000000000")
            assert "0000000000000000" in packets[press + 1:], "Q release missing"
            if not remap:
                default_paths.append([(e["ms"], e["event"], e["hex"]) for e in events
                                      if e["event"] in ("companion-submit", "HID-submit", "EP1-host-consume")])
            else:
                for packet in ("0000290000000000", "00002b0000000000", "08001d0000000000",
                               "0a001d0000000000", "0000520000000000"):
                    assert packet in packets, f"required NAV report missing: {packet}"
                    index = packets.index(packet)
                    assert "0000000000000000" in packets[index + 1:], f"release missing: {packet}"
                tap_start = next(i for i, e in enumerate(events) if e["event"] == "synthetic-TAP-configuration")
                tap_packets = [e["hex"] for e in events[tap_start:] if e["event"] == "EP1-host-consume"]
                tap_ok = tap_packets[-2:] == ["0000290000000000", "0000000000000000"]
                if name.endswith("negative"):
                    assert not tap_ok, "negative control failed to detect missing timer hook"
                else:
                    assert tap_ok, "idle tap release missing"
                    expected = ["0000140000000000", "0000000000000000"] * 2
                    for key in ("0000290000000000", "00002b0000000000", "08001d0000000000",
                                "0a001d0000000000", "0000520000000000", "0000290000000000"):
                        expected += [key, "0000000000000000"]
                    assert packets == expected, "extra, missing, or reordered HID packet"
            report["cases"].append({"name": name, "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                                    "packets": packets})
        assert default_paths[0] == default_paths[1], "stock-default OEM path differs"
        report["stock_default_equivalent"] = True
        report["passed"] = True
    finally:
        (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
