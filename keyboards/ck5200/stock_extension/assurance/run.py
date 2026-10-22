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
    descriptor = stock.read_bytes()[0x6580 - 0x2000:0x6580 - 0x2000 + 97]
    fields, page, size, count, offset = [], 0, 0, 0, 0
    while offset < len(descriptor):
        tag = descriptor[offset]
        width = (0, 1, 2, 4)[tag & 3]
        value = int.from_bytes(descriptor[offset + 1:offset + 1 + width], "little")
        offset += 1 + width
        kind = tag & 0xFC
        if kind == 0x04: page = value
        elif kind == 0x74: size = value
        elif kind == 0x94: count = value
        elif kind == 0x80: fields.append([page, size, count, value])
    assert fields == [[7,1,4,2], [7,4,1,3], [7,8,1,3], [7,8,4,0], [12,1,3,2], [12,13,1,3]], "unreviewed OEM HID layout"
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
             f"-DCK_ENGINE_PROCESS={symbols['ck_shim_process']}",
             "-I", source, "-I", HERE, "-include", HERE / "gate.h",
             *(source / (n + ".c") for n in names), HERE / "runner.c", "-o", OUT / "runner"])
    report = {"profile": "V122-bounded-scanner", "passed": False, "chgame_pin": PIN,
              "limitations": ["Synthetic GPIO closures, settings, and ready EP1 host.",
                              "CRT stops before SystemInit. Scanner initializer executes separately.",
                              "Timer driver executes through the genuine ISR JAL target, not IRQ entry/return.",
                              "No enumeration, iAP2 session, physical USB, install, or recovery proof."],
              "hid_input_fields": fields,
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
        compatibility_paths = []
        mixed_paths = []
        for name, image in [("stock", stock), ("extension", extension),
                            ("native-active", extension),
                            ("stock-compat", stock), ("native-compat", extension),
                            ("stock-mixed", stock), ("native-mixed", extension),
                            ("remap", extension), ("remap-delayed-host", extension),
                            ("remap-no-timer-negative", mutant),
                            *((name, extension) for name in (
                                "layers-sync", "layers-sym-off", "layers-nav-off", "layers-replace",
                                "layers-modes", "layers-base-sweep", "layers-sym-sweep", "layers-nav-sweep"))]:
            trace = OUT / f"{name}.trace.jsonl"
            remap = name.startswith("remap")
            layers = name.startswith("layers-")
            config = [hex(symbols[s]) for s in ("ck_remapping_enabled", "ck_extension_keymap")] if remap or layers or name.startswith("native") else []
            if layers:
                config.append(name)
            if name == "native-active":
                config.append("native-active")
            if name.endswith("compat") or name.endswith("mixed"):
                config.append(name.split("-")[-1])
            if name == "remap-delayed-host":
                config.append("delayed-host")
            command([OUT / "runner", image, trace, *config], env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            summary = events[-1]
            if name.startswith("native") or layers:
                assert summary["engine_calls"] > 0, "native comparison bypassed the enabled engine"
            if name.endswith("compat"):
                assert all(summary["currency_hits"]) and all(summary["layout_hits"]), "currency/layout path not executed"
                assert summary["sticky_hits"] and summary["cursor_hits"], "sticky/cursor path not executed"
                assert sum(e["event"] == "synthetic-OEM-settings" for e in events) == 130
            packets = [e["hex"] for e in events if e["event"] == "EP1-host-consume"]
            assert "0000140000000000" in packets, "Q press missing"
            press = packets.index("0000140000000000")
            assert "0000000000000000" in packets[press + 1:], "Q release missing"
            if layers:
                latest = None
                checks = 0
                for event in events:
                    if event["event"] == "EP1-host-consume":
                        latest = bytes.fromhex(event["hex"])
                    elif event["event"] == "layer-check":
                        expected = bytes([event["mods"], 0, event["usage"], event["usage2"], 0, 0, 0, 0])
                        assert latest == expected, f"{name}/{event['name']}: {latest.hex() if latest else None} != {expected.hex()}"
                        checks += 1
                assert checks >= (72 if name.endswith("sweep") else 3), "layer checks missing"
                if name == "layers-sync":
                    assert summary["sticky_mode_probes"] and summary["gui_mode_probes"], "sticky/GUI selector paths missing"
            elif name.endswith("mixed"):
                start = next(i for i, e in enumerate(events) if e["event"] == "mixed-mapping-scenario")
                compared = []
                for e in events[start:]:
                    if e["event"] not in ("companion-submit", "HID-submit", "EP1-host-consume"):
                        continue
                    data = bytearray.fromhex(e["hex"])
                    if name == "stock-mixed":
                        offset = 5 if e["event"] == "companion-submit" else 2
                        for index in range(offset, min(offset + 4, len(data))):
                            if data[index] == 0x14: data[index] = 0x29
                    compared.append((e["event"], data.hex()))
                mixed_paths.append(compared)
            elif name.endswith("compat"):
                compatibility_paths.append([(e["event"], e["hex"]) for e in events
                                            if e["event"] in ("companion-submit", "HID-submit", "EP1-host-consume")])
            elif not remap:
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
                                "0a001d0000000000", "0000520000000000", "0000185117290400",
                                "0000290000000000"):
                        expected += [key, "0000000000000000"]
                    assert packets == expected, "extra, missing, or reordered HID packet"
            report["cases"].append({"name": name, "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                                    "packets": packets, "coverage": summary,
                                    "layer_checks": sum(e["event"] == "layer-check" for e in events),
                                    "companion_submissions": sum(e["event"] == "companion-submit" for e in events)})
        assert default_paths[0] == default_paths[1], "stock-default OEM path differs"
        assert default_paths[0] == default_paths[2], "enabled native OEM path differs"
        assert compatibility_paths[0] == compatibility_paths[1], "enabled native behavior differs"
        assert mixed_paths[0] == mixed_paths[1], "native behavior differs around a remapped letter"
        report["stock_default_equivalent"] = True
        report["enabled_native_equivalent"] = True
        report["native_settings_equivalent"] = True
        report["native_custom_layer_checks"] = True
        report["mixed_sticky_mapping_equivalent"] = True
        report["passed"] = True
    finally:
        (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
