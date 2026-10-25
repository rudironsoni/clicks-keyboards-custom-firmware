#!/usr/bin/env python3
"""FIRST: execute one reviewed release, never rebuild or modify guest firmware."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import struct
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BUILD = ROOT / "build/assurance"
UPSTREAM = ROOT / "external/chgame_emulator"
PIN = "bb183d7863961b5f53dab3c8d3d3c7a9dc4b1a48"
WCH_PIN = "c6253942b29ff2a3f5e2829182a874afffa45881"
BIN_HASH = "969e6d71a177119e19dd0a93fffee4cfc63f2c1b46c01a9ccf01d56ac353694e"
ELF_CONTRACT_HASH = "55ebb1bd2db791ce7ed86b740750aa5e5f458179daf0a31a9ba41931c11685ea"
# Bind tracepoints to the exact BIN and the reviewed ELF contract.
# A new binary requires a reviewed profile, never automatic acceptance.
ANCHORS = {
    "STARTUP_MRET": 0x7d16, "SYSTEM_RETURN": 0x7d0a, "CONSUMED": 0x7d6c,
    "EARLY_ARM": 0x7c72, "EARLY_CRT": 0x7c8c,
    "TRANSPORT_RETURN": 0x7e76, "LOOP": 0x805e, "ROW_READ": 0x6160,
    "ROW_BOUND": 0x6178, "SCAN_DONE": 0x618a, "KEYBOARD_RETURN": 0x80b6,
    "KEYBOARD_CONNECTED_RETURN": 0x809a,
    "LOOP_BACK": 0x8116, "SYSTICK_MRET": 0x78aa, "USBFS_MRET": 0x7a38,
    "RESCUE_LOOP": 0x7e2a, "RESCUE_BACK": 0x7e60,
}
SYMBOLS = {
    "ENTRY": "_start", "RESET": "handle_reset", "SYSTEM": "SystemInit",
    "MAIN": "main", "KEYBOARD": "keyboard_task", "SYSTICK": "SysTick_Handler",
    "USBFS": "USBFS_IRQHandler",
    "USBD_DEV": "_usbd_dev",
    "KEYBOARD_PROTOCOL": "keyboard_protocol",
    "KEYBOARD_LEDS": "keyboard_led_state",
    "HIDD_XFER_CB": "hidd_xfer_cb", "KB_INFLIGHT": "keyboard_report_in_flight",
    "HID_COUNT": "hid_count",
    "TIMER": "timer_count", "NOINIT": "rescue_request", "RESCUE": "rescue",
    "GP": "__global_pointer$", "STACK": "_estack", "STACK_SIZE": "__stack_size", "DATA": "_data_vma",
    "DATA_LMA": "_data_lma", "DATA_END": "_edata", "BSS": "_sbss", "BSS_END": "_ebss",
    "WATCHDOG_FINISH": "ck5200_watchdog_finish", "WATCHDOG_PROGRESS": "watchdog_progress",
}
LIMITATIONS = [
    "Direct application entry at 0x2000, not OEM boot or installation evidence.",
    "Synthetic POR/warm resets; C8T chip ID is a model assumption, not a device observation.",
    "Host detach is modeled as absence of tokens then full bus reset; no physical unplug callback is invented.",
    "Matrix pin closure, GPIO/PWM behavior, and USB host packet timing are modeled assumptions, not hardware observations.",
    "EP0/EP1 only: no host iAP2 FF55/FF5A response, EA session, auth chip success, iPhone recognition, or accessory compatibility is established.",
    "Missing HID class callback and failed completion result are one-shot CPU fault inputs, not controller-produced errors.",
    "X035 MIS_ST settling time is unverified; the host model supplies stable suspend/active levels at IRQ entry.",
    "Fixed modeled 48 MHz, including watchdog cases past one full timeout. Timing is approximate, not hardware timing proof.",
    "First CCCC uses a conservative maximum 12-bit counter bound; exact internal count at enable is unverified. AAAA reload uses RLDR=4000.",
    "IWDG_SW option-bit state is unverified; the model starts with IWDG disabled after reset.",
    "Executed-path checks only, not total instruction proof or all firmware states.",
    "Internal tracepoints are exact-hash-bound. New artifacts require profile review.",
]

def command(args, timeout=60, cwd=None):
    p = subprocess.run([str(x) for x in args], cwd=cwd, capture_output=True, text=True,
                       timeout=timeout, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    if p.returncode:
        raise ValueError(f"{args[0]} exited {p.returncode}: {p.stdout}{p.stderr}")
    return p.stdout

def runner_record(mode, image):
    argv = [str(BUILD / "runner"), mode, str(image.resolve())]
    p = subprocess.run(argv, capture_output=True, text=True, timeout=60,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    return {"argv": argv, "exit_code": p.returncode,
            "stdout": p.stdout, "stderr": p.stderr, "result": json.loads(p.stdout)}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify_dependency(path, pin):
    if command(["git", "rev-parse", "HEAD"], cwd=path).strip() != pin:
        raise ValueError(f"dependency pin mismatch: {path}")
    if command(["git", "status", "--porcelain", "--untracked-files=all"], cwd=path).strip():
        raise ValueError(f"dependency is not clean: {path}")

def validator():
    spec = importlib.util.spec_from_file_location("validate_image", ROOT / "tools/validate_image.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def elf_contract_hash(elf):
    """Ignore compiler-comment text and file placement, never loaded bytes or symbols."""
    data = elf.read_bytes()
    def block(offset, size):
        if offset > len(data) or size > len(data) - offset:
            raise ValueError("ELF contract range exceeds file")
        return data[offset:offset+size]
    header = struct.unpack("<16sHHIIIIIHHHHHH", block(0,52))
    ident, kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shnames = header
    if ident[:7] != b"\x7fELF\x01\x01\x01" or (ehsize,phsize,shsize) != (52,32,40) or shnames >= shnum:
        raise ValueError("unsupported ELF contract format")
    sections = [struct.unpack("<10I",block(shoff+i*shsize,shsize)) for i in range(shnum)]
    names_section = sections[shnames]
    names = block(names_section[4],names_section[5])
    contract = {"header":[ident.hex(),kind,machine,version,entry,flags,ehsize,phsize,phnum,shsize,shnum,shnames],
                "sections":[], "segments":[]}
    for name_index,typ,attributes,address,offset,size,link,info,align,entsize in sections:
        if name_index >= len(names) or b"\0" not in names[name_index:]:
            raise ValueError("invalid ELF section name")
        name = names[name_index:].split(b"\0",1)[0].decode("ascii")
        contents = block(offset,size) if typ != 8 else b"" # NOBITS has no file payload.
        if name == ".comment":
            if attributes & 2:
                raise ValueError("allocated compiler comment is not metadata")
            size,contents = 0,b""
        contract["sections"].append([name,typ,attributes,address,size,link,info,align,entsize,
                                     hashlib.sha256(contents).hexdigest()])
    for i in range(phnum):
        typ,offset,vma,lma,filesz,memsz,attributes,align = struct.unpack("<8I",block(phoff+i*phsize,phsize))
        contract["segments"].append([typ,vma,lma,filesz,memsz,attributes,align,
                                     hashlib.sha256(block(offset,filesz)).hexdigest()])
    return hashlib.sha256(json.dumps(contract,sort_keys=True,separators=(",",":")).encode()).hexdigest()

def validate(image, elf):
    v = validator()
    data = image.read_bytes()
    v.validate_elf(elf, data, v.validate_binary(data))
    if digest(image) != BIN_HASH:
        raise ValueError("unreviewed artifact: exact BIN profile hash mismatch")
    if elf_contract_hash(elf) != ELF_CONTRACT_HASH:
        raise ValueError("unreviewed artifact: ELF runtime contract hash mismatch")
    symbols = v._symbols(v._run(v._tool("nm"), "-n", str(elf)))
    dis = v._run(v._tool("objdump"), "-d", str(elf))
    instructions = {}
    for line in dis.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{4,8})\s+(.+)", line)
        if match:
            address = int(match[1], 16)
            if address in instructions:
                raise ValueError("ambiguous disassembly instruction")
            instructions[address] = (match[2], match[3])
    profile = {name: symbols[symbol] for name, symbol in SYMBOLS.items()}
    profile.update(ANCHORS)
    layout = v._run(v._tool("readelf"), "-W", "-S", str(elf))
    text = re.findall(r"\]\s+\.text\s+PROGBITS\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", layout)
    if len(text) != 1:
        raise ValueError("missing/ambiguous .text")
    profile.update(TEXT=int(text[0][0],16), TEXT_END=int(text[0][0],16)+int(text[0][1],16),
                   IMAGE_END=0x2000+len(data))
    for name in [*ANCHORS, "ENTRY", "RESET", "SYSTEM", "MAIN", "KEYBOARD", "SYSTICK"]:
        address = profile[name]
        if address not in instructions:
            raise ValueError(f"missing tracepoint {name}")
        raw = instructions[address][0]
        if data[address-0x2000:address-0x2000+len(raw)//2] != bytes.fromhex(raw)[::-1]:
            raise ValueError(f"tracepoint bytes differ: {name}")
    return profile, {name: instructions[address] for name,address in ANCHORS.items()}

def build(profile, sanitizer):
    verify_dependency(UPSTREAM, PIN)
    verify_dependency(ROOT / "external/ch32x035", WCH_PIN)
    BUILD.mkdir(parents=True, exist_ok=True)
    sources = BUILD / "src"
    sources.mkdir(exist_ok=True)
    names = ["rv32", "bus", "machine", "st7735", "sdspi", "sdcard"]
    for name in [*(n+".c" for n in names), "machine.h", "sdspi.h", "sdcard.h"]:
        shutil.copyfile(UPSTREAM / "src" / name, sources / name)
    (BUILD / "profile.h").write_text("\n".join(f"#define P_{k} 0x{v:x}u" for k,v in profile.items())+"\n")
    command(["patch", "--batch", "--fuzz=0", "-p1", "-i", HERE / "upstream.patch"], cwd=sources)
    cc = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
          "-Wno-unused-parameter", "-I", sources, "-I", HERE, "-I", BUILD,
          "-include", HERE / "gate.h"]
    if sanitizer:
        cc += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    core = [sources / (n+".c") for n in names] + [HERE / "model.c"]
    command([*cc, *core, HERE / "runner.c", "-o", BUILD / "runner"])
    command([*cc, *core, UPSTREAM / "tests/xw_test.c", "-o", BUILD / "xw_test"])
    return command([BUILD / "xw_test", UPSTREAM / "tests/xw_golden.txt"])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--report", type=Path, default=BUILD / "report.json")
    parser.add_argument("--no-sanitize", action="store_true", help="explicitly skip host ASan/UBSan")
    args = parser.parse_args()
    report = {"gate": "FIRST-CK5200-v1", "passed": False, "failures": [], "limitations": LIMITATIONS,
              "chgame_pin": PIN, "register_reference_pin": WCH_PIN,
               "profile": "exact-release-connected-C8T", "modeled_budget_ms": 100,
               "maximum_progress_gap_cycles": 480000,
              "host_timeout_seconds_per_run": 60, "watchdog_timeout_seconds": 360,
              "sanitizers": not args.no_sanitize}
    report["model_inputs"] = {"entry":0x2000, "chip_id":0x03510601, "serial_word":0x12345678,
                              "cold_reset_flags":0x08000000, "warm_reset_flags":0x10000000,
                              "cold_ram_seed":"1 + (offset * 73) % 255",
                               "warm_ram":"retained from previous successful case",
                               "initial_mstatus":0x1800, "initial_mtvec":0,
                               "initial_general_registers":"zero",
                               "initial_PFIC":"no enabled or pending interrupts",
                               "initial_SysTick":"disabled",
                               "first_CCCC_conservative_max_cycles":134217728,
                               "configured_AAAA_reload_cycles":131104768,
                               "IWDG_SW":"assumed disabled after reset",
                               "USB":"modeled connected host",
                               "keys_pressed": ["row0/col0 Q", "row0/col5 consumer 0x00CF",
                                                "row5/col5 consumer 0x00B8"]}
    report["host_script"] = {"speed":"full", "address":5,
        "EP0":"real setup, data, and status IRQ45 transactions; all device/configuration/string/HID report descriptors",
        "EP1":"10-byte report protocol and 8-byte boot protocol, host ACK and OUT toggle",
        "matrix":"PB one-hot row drives PA pull-down columns; hold each transition 20 ms beyond 5 ms debounce",
        "detach":"no host tokens or invented firmware callback; reconnect uses bus reset and full enumeration",
        "backpressure_cycles":131104768,
        "packet_trace":"each connected case result includes exact consumed IN packets"}
    try:
        report["input_hashes"] = {"BIN": digest(args.image), "ELF": digest(args.elf)}
        profile, anchors = validate(args.image, args.elf)
        report.update(bounds_and_symbols=profile, tracepoints=anchors, byte_identity_verified=True,
                      elf_contract_sha256=ELF_CONTRACT_HASH,
                      elf_comparison="section and segment contract, excluding compiler-comment text and file offsets")
        report["upstream_xw_decode"] = build(profile, not args.no_sanitize).strip()
        report["model_fixtures"] = json.loads(command([BUILD/"runner", "--fixtures", UPSTREAM/"tests/xw_golden.txt"]))
        result = subprocess.run([str(BUILD/"runner"),str(args.image.resolve()),str(BUILD)],
                                capture_output=True, text=True, timeout=60,
                                env={**os.environ,"UBSAN_OPTIONS":"halt_on_error=1"})
        report["execution_exit_code"] = result.returncode
        if result.stderr: report["execution_stderr"] = result.stderr
        report["cases"] = json.loads(result.stdout)
        report["traces"] = {case["name"]: {"path":str(BUILD/(case["name"]+".trace.jsonl")),
                            "sha256":digest(BUILD/(case["name"]+".trace.jsonl"))} for case in report["cases"]}
        if result.returncode or len(report["cases"]) != 4 or not all(c["passed"] for c in report["cases"]):
            raise ValueError(f"execution obligations failed: {result.stderr}")
        report["late_stall_controls"] = json.loads(command([BUILD/"runner", "--stall-controls", args.image.resolve()]))
        if len(report["late_stall_controls"]) != 2 or not all(c["passed"] for c in report["late_stall_controls"]):
            raise ValueError("late-stall controls failed")
        report["watchdog_cases"] = json.loads(command([BUILD/"runner", "--watchdog", args.image.resolve()],timeout=360))
        if len(report["watchdog_cases"]) != 13 or not all(c["passed"] for c in report["watchdog_cases"]):
            raise ValueError("watchdog fault/reset controls failed")
        report["watchdog_mutant_controls"] = json.loads(command([BUILD/"runner", "--watchdog-mutants", args.image.resolve()],timeout=360))
        if not report["watchdog_mutant_controls"]["passed"]:
            raise ValueError("watchdog mutant controls failed")
        report["usb_negative_controls"] = runner_record("--usb-fixtures", args.image)
        report["usb_model_mutants"] = runner_record("--usb-mutants", args.image)
        if any(r["exit_code"] or not r["result"]["passed"] for r in
               (report["usb_negative_controls"], report["usb_model_mutants"])):
            raise ValueError("connected USB model controls failed")
        report["connected_cases"] = [runner_record(mode, args.image) for mode in
            ("--connected", "--missing-callback", "--failed-completion",
             "--disconnect-reconnect", "--connected-backpressure", "--stalled-usb-irq",
             "--connected-suspend-idle", "--stopped-cnt-resume")]
        if any(r["exit_code"] or not r["result"]["passed"] for r in report["connected_cases"]):
            raise ValueError("compiled connected USB scenario failed")
        report["resume_case"] = runner_record("--resume-counterexample", args.image)
        # The tests also exercise pre-execution rejection and failure reports.
        report["artifact_controls"] = json.loads(command([sys.executable,HERE/"test_artifact.py",args.image.resolve(),args.elf.resolve()]))
        verify_dependency(UPSTREAM, PIN)
        verify_dependency(ROOT / "external/ch32x035", WCH_PIN)
        if report["resume_case"]["exit_code"] or not report["resume_case"]["result"]["passed"]:
            raise ValueError("compiled X035 suspend/resume transition failed")
        report["passed"] = True
    except (ValueError, OSError, subprocess.TimeoutExpired, KeyError, struct.error) as error:
        report["failures"].append(str(error))
    finally:
        if args.image.is_file() and args.elf.is_file():
            report["final_hashes"] = {"BIN": digest(args.image), "ELF": digest(args.elf)}
            if report.get("input_hashes") != report["final_hashes"]:
                report["passed"] = False
                report["failures"].append("input artifacts changed during gate")
        report["assurance_sources"] = {p.name:digest(p) for p in HERE.iterdir() if p.is_file()}
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+"\n")
    print(f"FIRST {'PASS' if report['passed'] else 'FAIL'}: {args.report}")
    for failure in report["failures"]: print(failure, file=sys.stderr)
    return 0 if report["passed"] else 1

if __name__ == "__main__":
    sys.exit(main())
