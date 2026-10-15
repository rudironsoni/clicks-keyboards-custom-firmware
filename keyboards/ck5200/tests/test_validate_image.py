"""Mutate real production ELF section headers, then run the full validator."""
from pathlib import Path
import importlib.util
import struct

import pytest

ROOT = Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location("validate_image", ROOT / "tools/validate_image.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


@pytest.mark.parametrize("fault", [
    None, "missing", "initialized", "oversized", "bss-overlap", "stack-overlap", "section-overlap",
])
def test_rescue_noinit_contract(tmp_path, fault):
    image = (ROOT / "build/ck5200_qmk.bin").read_bytes()
    elf = bytearray((ROOT / "build/ck5200_qmk.elf").read_bytes())
    # ELF32 little-endian section table: sh_name/type/flags/addr/offset/size.
    assert elf[:6] == b"\x7fELF\x01\x01"
    table = struct.unpack_from("<I", elf, 32)[0]
    entry_size, count, strings_index = struct.unpack_from("<HHH", elf, 46)
    strings_offset = struct.unpack_from("<I", elf, table + strings_index * entry_size + 16)[0]
    sections = {}
    for index in range(count):
        offset = table + index * entry_size
        name_offset = strings_offset + struct.unpack_from("<I", elf, offset)[0]
        name = elf[name_offset:elf.index(0, name_offset)].decode()
        sections[name] = offset
    noinit = sections[".noinit"]
    address = struct.unpack_from("<I", elf, noinit + 12)[0]
    if fault == "missing":
        struct.pack_into("<I", elf, noinit, 0)  # Remove the required section name.
    elif fault == "initialized":
        struct.pack_into("<I", elf, noinit + 4, 1)  # SHT_PROGBITS, not SHT_NOBITS.
    elif fault == "oversized":
        struct.pack_into("<I", elf, noinit + 20, 12)
    elif fault == "bss-overlap":
        struct.pack_into("<I", elf, noinit + 12, address - 4)
    elif fault == "stack-overlap":
        struct.pack_into("<I", elf, noinit + 12, m.RAM_LIMIT - 4)
    elif fault == "section-overlap":
        struct.pack_into("<I", elf, sections["._user_heap_stack"] + 12, address + 4)
    path = tmp_path / "firmware.elf"
    path.write_bytes(elf)
    reset = m.validate_binary(image)
    if fault is None:
        m.validate_elf(path, image, reset)
    else:
        with pytest.raises(ValueError, match="section|rescue request"):
            m.validate_elf(path, image, reset)
