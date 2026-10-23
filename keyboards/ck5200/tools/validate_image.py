#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile

APP_BASE = 0x2000
MAX_IMAGE_SIZE = 0x6A00
APP_LIMIT = APP_BASE + MAX_IMAGE_SIZE
RAM_BASE = 0x20000000
RAM_LIMIT = 0x20005000


def validate_flash_access(disassembly: str) -> None:
    """Compiler-specific tripwire, not a proof of arbitrary C memory safety.

    The pinned build inlines the flash SDK in staging_write_page. SystemInit
    only sets flash latency. Any other flash-controller reference needs review.
    """
    function = ""
    for line in disassembly.splitlines():
        match = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if match:
            function = match.group(1)
        if re.search(r"\blui\s+\w+,\s*0x40022\b", line):
            if function != "SystemInit" and not re.fullmatch(r"staging_write_page(?:\.part\.\d+)?", function):
                raise ValueError(f"unexpected flash-controller access in {function}")


def _reset_target(instruction: int) -> int:
    if instruction & 0x7F != 0x6F:
        raise ValueError(f"first instruction is not a JAL reset jump: 0x{instruction:08x}")
    if (instruction >> 7) & 0x1F:
        raise ValueError("reset JAL must use x0 as its destination")
    immediate = (
        (((instruction >> 31) & 1) << 20)
        | (((instruction >> 21) & 0x3FF) << 1)
        | (((instruction >> 20) & 1) << 11)
        | (((instruction >> 12) & 0xFF) << 12)
    )
    if immediate & (1 << 20):
        immediate -= 1 << 21
    return APP_BASE + immediate


def validate_binary(data: bytes) -> int:
    """Validate an application binary without touching a device."""
    if not data:
        raise ValueError("empty image")
    if len(data) > MAX_IMAGE_SIZE:
        raise ValueError(f"image too large: 0x{len(data):x} > 0x{MAX_IMAGE_SIZE:x}")
    if len(data) < 8:
        raise ValueError("image too short for reset and first vector entry")

    target = _reset_target(struct.unpack_from("<I", data)[0])
    image_end = APP_BASE + len(data)
    if not APP_BASE <= target < image_end:
        raise ValueError(f"reset target 0x{target:x} is outside image 0x{APP_BASE:x}..0x{image_end:x}")
    if target & 1:
        raise ValueError(f"reset target is not 2-byte aligned: 0x{target:x}")

    vector_start = struct.unpack_from("<I", data, 4)[0]
    if vector_start != APP_BASE:
        raise ValueError(f"first vector target is 0x{vector_start:x}, expected 0x{APP_BASE:x}")
    return target


def _tool(name: str) -> str:
    prefixes = ("riscv-none-elf-", "riscv-none-embed-")
    for prefix in prefixes:
        found = shutil.which(prefix + name)
        if found:
            return found
    toolchains = pathlib.Path(__file__).resolve().parents[1] / ".toolchains"
    found = sorted(
        path for prefix in prefixes
        for path in toolchains.glob(f"*/bin/{prefix}{name}") if path.is_file()
    )
    if len(found) == 1:
        return str(found[0])
    if len(found) > 1:
        raise ValueError(f"multiple RISC-V {name} tools found; put the selected toolchain on PATH")
    raise ValueError(f"RISC-V {name} is required for ELF validation")


def _run(tool: str, *args: str) -> str:
    try:
        result = subprocess.run([tool, *args], capture_output=True, text=True, check=False)
    except OSError as error:
        raise ValueError(f"could not run {tool}: {error}") from error
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ValueError(f"{pathlib.Path(tool).name} failed: {detail}")
    return result.stdout


def _symbols(output: str) -> dict[str, int]:
    symbols = {}
    for line in output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            symbols[fields[2]] = int(fields[0], 16)
    return symbols


def validate_elf(elf: pathlib.Path, data: bytes, reset: int) -> None:
    output = _run(_tool("readelf"), "-W", "-h", "-S", "-l", str(elf))
    symbols = _symbols(_run(_tool("nm"), "-n", str(elf)))
    elf_type = re.search(r"^\s*Type:\s+(\S+)", output, re.MULTILINE)
    entry = re.search(r"Entry point address:\s*0x([0-9a-fA-F]+)", output)
    if not elf_type or elf_type.group(1) != "EXEC" or not entry:
        raise ValueError("ELF is not an executable with an entry point")
    if int(entry.group(1), 16) != APP_BASE:
        raise ValueError(f"ELF entry must be 0x{APP_BASE:x}")

    section_pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+"
        r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+"
        r"[0-9a-fA-F]+\s+(\S*)",
        re.MULTILINE,
    )
    section_rows = section_pattern.findall(output)
    sections = {
        name: (int(address, 16), int(size, 16), flags)
        for name, _kind, address, size, flags in section_rows
    }
    required_sections = (".init", ".vector", ".text", ".data", ".bss", ".noinit")
    if any(name not in sections for name in required_sections):
        raise ValueError("ELF is missing a required firmware section")

    # Fail closed on new allocated sections or executable RAM. Empty linker
    # placeholders (e.g. .fini) carry flags but occupy no storage.
    allowed = {".init": ("PROGBITS", "AX"), ".vector": ("PROGBITS", "AX"),
               ".text": ("PROGBITS", "AX"), ".data": ("PROGBITS", "WA"),
               ".bss": ("NOBITS", "WA"), ".noinit": ("NOBITS", "WA"),
               "._user_heap_stack": ("NOBITS", "WA")}
    for name, kind, address, size, flags in section_rows:
        start, length = int(address, 16), int(size, 16)
        if "A" not in flags or not length:
            continue
        if name not in allowed or (kind, flags) != allowed[name]:
            raise ValueError(f"unexpected allocated section or flags: {name} {kind} {flags}")
        low, high = (APP_BASE, APP_LIMIT) if "X" in flags else (RAM_BASE, RAM_LIMIT)
        if not low <= start < start + length <= high:
            raise ValueError(f"ELF section {name} is outside its permitted region")

    for name in symbols:
        if name.startswith("FLASH_") and name != "FLASH_IRQHandler":
            raise ValueError(f"unexpected linked flash SDK function: {name}")
        if "wear_leveling" in name or "eeprom_driver_erase" in name:
            raise ValueError(f"unexpected persistent EEPROM function: {name}")

    required_symbols = (
        "_start", "handle_reset", "_vector_base", "_data_lma", "_data_vma",
        "_edata", "_sbss", "_ebss", "_eusrstack", "_estack",
        "__global_pointer$", "__stack_size",
    )
    missing = [name for name in required_symbols if name not in symbols]
    if missing:
        raise ValueError(f"ELF is missing startup symbols: {', '.join(missing)}")

    init, vector = sections[".init"], sections[".vector"]
    if init[0] != APP_BASE or symbols["_start"] != APP_BASE or init[1] < 4:
        raise ValueError("reset stub does not start at the application base")
    if "AX" not in init[2] or vector[0] != symbols["_vector_base"] or vector[1] < 4 or vector[1] % 4:
        raise ValueError("vector section does not match its linked address")
    code_ranges = []
    for name in (".init", ".vector", ".text"):
        address, size, _flags = sections[name]
        if address < APP_BASE or address + size > APP_LIMIT:
            raise ValueError(f"ELF section {name} exceeds application flash")
        if name != ".vector":
            code_ranges.append((address, address + size))

    if symbols["handle_reset"] != reset:
        raise ValueError("reset JAL target does not match ELF handle_reset")
    vector_offset = vector[0] - APP_BASE
    if vector_offset < 0 or vector_offset + vector[1] > len(data):
        raise ValueError("vector section extends beyond the binary")
    targets = struct.unpack_from(f"<{vector[1] // 4}I", data, vector_offset)
    image_end = APP_BASE + len(data)
    if targets[0] != symbols["_start"]:
        raise ValueError("first vector target does not match ELF _start")
    for index, target in enumerate(targets):
        if target and (target & 1 or not any(start <= target < end for start, end in code_ranges)):
            raise ValueError(f"vector[{index}] target 0x{target:x} is outside executable code")

    data_vma, data_lma = symbols["_data_vma"], symbols["_data_lma"]
    data_end, bss_start, bss_end = symbols["_edata"], symbols["_sbss"], symbols["_ebss"]
    data_size = sections[".data"][1]
    bss_address, bss_size, _flags = sections[".bss"]
    if data_vma != sections[".data"][0] or data_end - data_vma != data_size:
        raise ValueError(".data symbols do not match the linked section")
    if bss_start != bss_address or bss_end - bss_start != bss_size or bss_start != data_end:
        raise ValueError(".bss symbols do not match startup clear bounds")
    if not RAM_BASE <= data_vma <= data_end <= bss_start <= bss_end <= RAM_LIMIT:
        raise ValueError(".data or .bss startup addresses are outside RAM")
    if not APP_BASE <= data_lma or data_lma + data_size > min(APP_LIMIT, image_end):
        raise ValueError(".data startup source is outside application flash")
    if not RAM_BASE <= symbols["__global_pointer$"] < RAM_LIMIT:
        raise ValueError("global pointer is outside RAM")
    if symbols["_eusrstack"] != RAM_LIMIT or symbols["_estack"] != RAM_LIMIT:
        raise ValueError("startup stack top does not match the end of RAM")
    stack_size = symbols["__stack_size"]
    if not 0 < stack_size <= RAM_LIMIT - RAM_BASE or bss_end > RAM_LIMIT - stack_size:
        raise ValueError("startup stack size is outside RAM")

    noinit = [row for row in section_rows if row[0] == ".noinit"]
    if len(noinit) != 1 or noinit[0][1] != "NOBITS":
        raise ValueError("rescue request requires exactly one .noinit NOBITS section")
    address, size, flags = sections[".noinit"]
    if size != 8 or address % 4 or address < bss_end or address + size > RAM_LIMIT - stack_size or flags != "WA":
        raise ValueError("rescue request must occupy eight aligned RAM bytes outside BSS and stack")
    for name, _kind, other_address, other_size, other_flags in section_rows:
        start, length = int(other_address, 16), int(other_size, 16)
        if name != ".noinit" and "A" in other_flags and length and start < address + size and address < start + length:
            raise ValueError(f"rescue request overlaps allocated section {name}")

    load_segments = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[0] == "LOAD":
            load_segments.append(tuple(int(value, 16) for value in fields[1:6]))
    data_loads = [
        segment for segment in load_segments
        if segment[1] == data_vma and segment[2] == data_lma
    ]
    if len(data_loads) != 1:
        raise ValueError("ELF data load segment does not match startup addresses")
    _offset, _vma, _lma, file_size, memory_size = data_loads[0]
    if file_size != data_size or memory_size != bss_end - data_vma:
        raise ValueError("ELF data load segment does not match startup sizes")
    for _offset, vma, lma, file_size, memory_size in load_segments:
        if file_size > memory_size:
            raise ValueError("ELF load segment file size exceeds memory size")
        if file_size and not APP_BASE <= lma < lma + file_size <= min(APP_LIMIT, image_end):
            raise ValueError("ELF load segment overlaps installer or exceeds application flash")
        if APP_BASE <= vma < APP_LIMIT:
            if vma + memory_size > APP_LIMIT or lma != vma:
                raise ValueError("ELF flash load segment exceeds application flash")
        elif RAM_BASE <= vma < RAM_LIMIT:
            if vma + memory_size > RAM_LIMIT:
                raise ValueError("ELF RAM load segment exceeds RAM")
            if file_size and not APP_BASE <= lma < APP_LIMIT:
                raise ValueError("ELF RAM initializer is outside application flash")
        else:
            raise ValueError(f"ELF load segment address 0x{vma:x} is outside flash and RAM")

    with tempfile.TemporaryDirectory(prefix="ck5200-image-") as temp_dir:
        binary_from_elf = pathlib.Path(temp_dir) / "image.bin"
        _run(_tool("objcopy"), "-O", "binary", str(elf), str(binary_from_elf))
        if binary_from_elf.read_bytes() != data:
            raise ValueError("binary does not match the ELF loadable image")

    validate_flash_access(_run(_tool("objdump"), "-d", str(elf)))


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a CK-5200 application image")
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--elf", type=pathlib.Path, help="matching ELF for full layout checks")
    args = parser.parse_args()
    data = args.image.read_bytes()
    try:
        reset = validate_binary(data)
        if args.elf:
            validate_elf(args.elf, data, reset)
    except ValueError as error:
        raise SystemExit(str(error)) from error

    instruction = struct.unpack_from("<I", data)[0]
    print(f"image:  {args.image}")
    print(f"size:   {len(data)} / 0x{len(data):x} (max 0x{MAX_IMAGE_SIZE:x}, 0x{APP_BASE:x}..0x{APP_LIMIT:x})")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")
    print(f"reset:  0x{instruction:08x} (JAL -> 0x{reset:x})")
    if args.elf:
        print(f"layout: {args.elf} verified against image and RAM bounds")
    else:
        print("layout: binary-only checks; ELF startup RAM layout not checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
