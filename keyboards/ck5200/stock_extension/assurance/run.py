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


def configuration_path(image, name):
    """Byte link enters OEM 0x2208; no keymap or enable-byte RAM injection."""
    child = subprocess.Popen([str(OUT / "runner"), str(image), str(OUT / f"{name}.trace.jsonl"), "config-server"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                             env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    def line(text):
        child.stdin.write(text + "\n"); child.stdin.flush()
        return child.stdout.readline().strip()
    def packet(op, generation=0, offset=0, data=b"", status=0):
        frame = bytes([12 + len(data), 0x94, 1, op]) + bytes.fromhex("78563412") + struct.pack("<HH", generation, offset) + data
        result = line("p " + frame.hex())
        assert result.startswith("R "), result
        reply = bytes.fromhex(result[2:])
        if name == "configuration-stock":
            assert reply[3] != 0 and b"CKX1" not in reply
        else:
            assert reply[:10] == bytes([len(reply), 2, 0x94, status, 1, op]) + frame[4:8], reply.hex()
        return reply[12:]
    def read(length, generation):
        return b"".join(packet(1, generation, offset, bytes([min(24, length - offset)])) for offset in range(0, length, 24))
    def stage(record, generation):
        packet(2, generation, len(record))
        for offset in range(0, len(record), 24): packet(3, generation, offset, record[offset:offset + 24])
    def keys(rows, expected):
        assert line("k " + bytes(rows).hex()) == "H " + expected
    try:
        assert child.stdout.readline().strip() == "ready"
        caps = packet(0)
        if name == "configuration-stock": return
        assert line("r ") == "U 00" # Initial enumeration reset must not strand disabled remaps.
        assert line("c ") == "U 01"
        caps = packet(0)
        assert line("p 0294") == "R" # No short-frame reads or mutation.
        assert caps[:12] == b"CKX1\x04\x03\x06\x06\xd2\x03\x18\x01"
        assert len(caps) == 15 and caps[14] == 0
        original = read(int.from_bytes(caps[12:14], "little"), 0)
        assert len(original) == 330
        # Replace SYM Q with Esc and NAV 4/4 with Tab in the packed map.
        custom = bytearray(original[:6])
        for offset in range(6, len(original), 3):
            pos, layer, flags = original[offset:offset + 3]
            custom += bytes([pos, layer, 0, 0x29 if layer == 1 else 0x2B, 0]) if (layer, pos) in ((1,0),(2,28)) else bytes([pos,layer,flags])
        stage(custom, 0); packet(4, data=b"\x01")
        assert read(len(custom), 1) == custom
        keys([0,0,0,0,0,0x10], "0000000000000000")
        keys([1,0,0,0,0,0x10], "0000290000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        keys([0,0,0x20,0,0,0x10], "0000000000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        keys([0,0,0,0,0x10,0], "00002b0000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        stage(original, 1)
        keys([1,0,0,0,0,0], "0000140000000000")
        packet(4, 1, data=b"\x01", status=2) # Held-key apply must not mutate.
        keys([0,0,0,0,0,0], "0000000000000000")
        packet(5, 1)
        malformed = bytearray(custom); malformed[5] |= 0x80
        stage(malformed, 1); packet(4, 1, data=b"\x01", status=1)
        assert read(len(custom), 1) == custom
        packet(2, 1, len(original)); packet(3, 1, data=original[:24])
        packet(4, 1, data=b"\x01", status=1) # Interrupted transfer stays inactive.
        packet(0) # A new handshake discards the incomplete record.
        packet(4, 1, data=b"\x01", status=1)
        assert read(len(custom), 1) == custom
        packet(2, 0, len(original), status=4) # Stale generation.
        maximum = bytearray(b"CKM\x04\x6c\x06")
        for layer in range(3):
            for pos in range(36): maximum += bytes([pos,layer,0x1D,0x10 | layer,15,255,0,255,255])
        assert len(maximum) == 978
        stage(maximum, 1); packet(4, 1, data=b"\x00")
        assert read(978, 2) == maximum
        stage(custom, 2); packet(4, 2, data=b"\x01")
        assert read(len(custom), 3) == custom
        keys([1,0,0,0,0,0], "0000140000000000")
        assert line("s 01") == "U 08"
        # No host completion during suspend. The queued release reaches
        # both transports after the real OEM resume interrupt.
        assert line("k 000000000000") == "H 0000140000000000"
        assert line("s 00") == "U 00"
        keys([0,0,0,0,0,0], "0000000000000000")
        packet(2, 3, len(original)); packet(3, 3, data=original[:24])
        assert line("r ") == "U 00"
        assert line("c ") == "U 01" # Configuration handoff remains synthetic.
        packet(1, 3, data=b"\x06", status=3) # Reset invalidates old session.
        packet(0); packet(4, 3, data=b"\x01", status=1)
        assert read(len(custom), 3) == custom
        assert line("j ") == "U armed"
        keys([0,0,0,0,0,0x10], "0000000000000000")
        keys([1,0,0,0,0,0x10], "0000000000000000") # Reset arrives during preview, before host consumption.
        keys([0,0,0,0,0,0], "0000000000000000")
        assert line("c ") == "U 01"
        packet(1, 3, data=b"\x06", status=3)
        packet(0)
        assert read(len(custom), 3) == custom
        assert line("b 01") == "U 01"
        keys([1,0,0,0,0,0], "0000140000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        keys([0,0,0x20,0,0,0x10], "0000000000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        for _ in range(12):
            line("k 010000000000"); line("k 000000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
        assert line("b 00") == "U 00"
        keys([0,0,0,0,0,0], "0000000000000000")
        keys([1,0,0,0,0,0], "0000140000000000")
        keys([0,0,0,0,0,0], "0000000000000000")
    finally:
        if child.poll() is None:
            child.stdin.write("q\n"); child.stdin.flush()
        child.wait(timeout=10)
        assert child.returncode == 0
    events = [json.loads(line) for line in (OUT / f"{name}.trace.jsonl").read_text().splitlines()]
    summary = events[-1]
    assert summary["irq_entries"] == summary["irq_returns"]
    assert any(e["event"] == "queue-fault-transition" and e["fault"] and e["pending"] == 8 for e in events), "overflow not reached"
    assert any(e["event"] == "reset-deferred-during-preview" for e in events), "reset interleaving not reached"
    assert [e["hex"] for e in events if e["event"] == "EP1-host-consume"][-1] == "0000000000000000"
    assert [e["hex"] for e in events if e["event"] == "EP3-host-consume"][-1] == "0b02000000000000000000"
    blocked = next(i for i,e in enumerate(events) if e["event"] == "companion-backpressure" and e["blocked"])
    unblocked = next(i for i,e in enumerate(events) if e["event"] == "companion-backpressure" and not e["blocked"])
    assert not any(e["event"] == "EP3-host-consume" for e in events[blocked:unblocked])
    assert "04040101" in [e["hex"] for e in events[unblocked:] if e["event"] == "EP3-host-consume"], "Cursor notification lost to backpressure"
    return summary


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
    build_sources = json.loads((OUT.parent / "build-source-sha256.json").read_text())
    for name, digest in build_sources.items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, f"stale firmware build: {name}"
    elf_bin = OUT / "elf-extracted.bin"
    command([toolchain / "riscv-none-elf-objcopy", "-O", "binary", elf, elf_bin])
    assert elf_bin.read_bytes() == (OUT.parent / "ck5200_extension.bin").read_bytes(), "BIN/ELF mismatch"
    command([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
             "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
             f"-DCK_BSS_START={symbols['__extension_bss_start']}",
             f"-DCK_BSS_END={symbols['__extension_bss_end']}",
             f"-DCK_ENGINE_PROCESS={symbols['ck_shim_process']}",
             f"-DCK_PREVIEWING={symbols['previewing']}",
             f"-DCK_CAPTURE={symbols['capturing_native']}",
             f"-DCK_PREVIEW_FRAME={symbols['preview_frame']}",
             f"-DCK_QUEUE_FAULT={symbols['queue_fault']}",
             f"-DCK_PENDING_COUNT={symbols['pending_count']}",
             f"-DCK_CAPTURE_PWM={symbols['ck_capture_pwm']}",
             "-I", source, "-I", HERE, "-include", HERE / "gate.h",
             *(source / (n + ".c") for n in names), HERE / "runner.c", "-o", OUT / "runner"])
    report = {"profile": "V122-bounded-scanner", "passed": False, "chgame_pin": PIN,
              "limitations": ["Synthetic GPIO, settings, USB configuration and authenticated companion state.",
                              "CRT stops before SystemInit. Scanner initializer executes separately.",
                              "Most scanner cases invoke the ISR JAL target. IRQ/configuration cases enter real SysTick and USB ISRs.",
                              "No EP0 enumeration, iAP2 authentication, full startup, physical USB, install, or recovery proof."],
              "hid_input_fields": fields,
              "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
              "build_source_sha256": build_sources,
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [HERE / "run.py", HERE / "runner.c", HERE / "gate.h",
                                          ROOT / "assurance/upstream.patch"]},
              "cases": []}
    try:
        flash_replies = []
        for mode in ("flash-write-probe", "flash-protected-probe"):
            path = OUT / f"{mode}.trace.jsonl"
            command([OUT / "runner", stock, path, mode],
                    env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            events = [json.loads(line) for line in path.read_text().splitlines()]
            flash_replies.append([e["hex"] for e in events if e["event"] == "stock-update-reply"])
            report["cases"].append({"name": mode, **events[-1]})
        assert flash_replies[0] == flash_replies[1], "protection changed stock acknowledgements"
        extension = OUT.parent / "ck5200_extension.bin"
        mutant = OUT / "no-timer-hook.bin"
        changed = bytearray(extension.read_bytes())
        struct.pack_into("<I", changed, 0x3AEE - 0x2000, 0xFECFE0EF)
        mutant.write_bytes(changed)
        pwm_mutant = OUT / "no-preview-pwm-hook.bin"
        changed = bytearray(extension.read_bytes())
        struct.pack_into("<I", changed, 0x3062 - 0x2000, 0x697010EF)
        pwm_mutant.write_bytes(changed)
        default_paths = []
        compatibility_paths = []
        mixed_paths = []
        for name, image in [("stock", stock), ("extension", extension),
                            ("native-active", extension),
                            ("stock-compat", stock), ("native-compat", extension),
                            ("stock-mixed", stock), ("native-mixed", extension),
                            ("remap", extension), ("remap-delayed-host", extension),
                            ("remap-irq", extension),
                            ("remap-no-timer-negative", mutant),
                            *((name, extension) for name in (
                                "layers-sync", "layers-sym-off", "layers-nav-off", "layers-replace",
                                "layers-modes", "layers-base-sweep", "layers-sym-sweep", "layers-nav-sweep")),
                            ("preview-side-effect-negative", pwm_mutant)]:
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
            if name == "remap-irq":
                config.append("real-irq")
            if name == "preview-side-effect-negative":
                config = [hex(symbols[s]) for s in ("ck_remapping_enabled", "ck_extension_keymap")] + ["layers-replace"]
                result = subprocess.run([str(OUT / "runner"), str(image), str(trace), *config],
                                        capture_output=True, text=True, timeout=120,
                                        env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
                assert result.returncode == 1 and "preview write outside stack or owned capture state" in result.stderr and "40012c20" in result.stderr, result.stderr
                report["cases"].append({"name": name, "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                                        "expected_failure": result.stderr.strip()})
                continue
            command([OUT / "runner", image, trace, *config], env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            summary = events[-1]
            if name.startswith("native") or layers:
                assert summary["engine_calls"] > 0, "native comparison bypassed the enabled engine"
            if name == "remap-irq":
                assert summary["irq_entries"] == summary["irq_returns"] and summary["usb_entries"] > 0
                assert summary["nested_entries"] > 0 and summary["deferred_usb"] and summary["preview_writes"] > 0
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
                latest_companion = None
                checks = 0
                for event in events:
                    if event["event"] == "EP1-host-consume":
                        latest = bytes.fromhex(event["hex"])
                    elif event["event"] == "companion-submit":
                        latest_companion = bytes.fromhex(event["hex"])
                    elif event["event"] == "projected-consumer-check":
                        expected = bytes([0,0,0,0,0,0,event["consumer"],0])
                        companion = b"\x0b\x02\x00" + expected + (b"\x04\x04\x01\x00" if event["cursor_exit"] else b"")
                        assert latest == expected and latest_companion == companion, event
                    elif event["event"] == "backlight-check":
                        assert event["pwm_writes"] == 2 and event["suppressed"] > 0 and event["flags"] == 0xA3, event
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
                                    "projected_consumer_checks": sum(e["event"] == "projected-consumer-check" for e in events),
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
        configuration_path(stock, "configuration-stock")
        report["configuration_coverage"] = configuration_path(extension, "configuration-extension")
        report["ram_configuration_execution"] = True
        report["irq_suspend_reset_backpressure_overflow"] = True
        report["passed"] = True
    finally:
        (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
