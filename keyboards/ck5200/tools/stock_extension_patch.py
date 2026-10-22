#!/usr/bin/env python3
"""Research tool: apply or reverse the stock-extension registration redirect.

Replaces the two instruction words that construct the V122 report-builder
callback pointer (lui a2,0x2 / addi a2,a2,0x796 = 0x2796) with words that
build the extension address 0x6900 (lui a2,0x7 / addi a2,a2,-0x700), so the
scanner registration stores the extension as the HID report builder.

OFFLINE RESEARCH ARTIFACT ONLY. A patched image is not authorized for
installation; the R3 recovery gate is unchanged. See
keyboards/ck5200/docs/stock-extension-feasibility.md.
"""
import argparse
import hashlib
import json
import pathlib
import sys

BASE = 0x2000
IMAGE_SHA256 = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"

# Registration-site words (linked 0x2A3A / 0x2A42; file offsets 0xA3A/0xA42).
PATCH_SITES = {
    0x2A3A: {"original": 0x00002637, "extension": 0x00007637},  # lui a2, 0x2 -> lui a2, 0x7
    0x2A42: {"original": 0x79660613, "extension": 0x90060613},  # addi a2, 0x796 -> addi a2, a2, -0x700
}
# Resulting pointer values: 0x2000+0x796 = 0x2796 (OEM) / 0x7000-0x700 = 0x6900
# (extension). The addi immediate is SIGNED 12-bit: 0x900 does not fit as +2304,
# so the decomposition uses 0x7<<12 minus 0x700.
EXTENSION_BUILDER = 0x6900
OEM_BUILDER = 0x2796


def _word(data: bytes, linked: int) -> int:
    off = linked - BASE
    return int.from_bytes(data[off:off + 4], "little")


def check_image(data: bytes) -> None:
    got = hashlib.sha256(data).hexdigest()
    if got != IMAGE_SHA256:
        raise SystemExit(f"refused: image sha256 {got} is not the pinned V122 artifact")


def patch_sites(startup_target=None, tick_target=None, layer_target=None,
                companion_target=None, hid_target=None, command_target=None, usb_reset_target=None, pwm_target=None):
    sites = dict(PATCH_SITES)
    for address, original, target, rd in ((0x48CE, 0xAECFF0EF, startup_target, 1),
                                         (0x3AEE, 0xFECFE0EF, tick_target, 1),
                                         (0x4788, 0x9A9FD2EF, companion_target, 0),
                                         (0x3CBE, 0xC72FE2EF, hid_target, 0),
                                         (0x2218, 0x7FF000EF, command_target, 1),
                                         (0x3D5E, 0x400237B7, usb_reset_target, 0),
                                         (0x3062, 0x697010EF, pwm_target, 1)):
        if target is None:
            continue
        delta = target - address
        if target < EXTENSION_BUILDER or target >= 0x8A00 or delta & 1:
            raise ValueError("extension hook target is outside aligned extension flash")
        imm = delta & 0x1FFFFF
        word = (((imm >> 20) & 1) << 31 | ((imm >> 1) & 0x3FF) << 21
                | ((imm >> 11) & 1) << 20 | ((imm >> 12) & 0xFF) << 12 | rd << 7 | 0x6F)
        sites[address] = {"original": original, "extension": word}
    if layer_target is not None:
        if not EXTENSION_BUILDER <= layer_target < 0x8A00 or layer_target & 1:
            raise ValueError("layer hook target is outside aligned extension flash")
        sites[0x2A36] = {"original": 0x000036B7,
                         "extension": ((layer_target + 0x800) & ~0xFFF) | 0x6B7}
        sites[0x2A3E] = {"original": 0x95868693,
                         "extension": (layer_target & 0xFFF) << 20 | 0x68693}
    return sites


def apply_patch(data: bytes, direction: str, startup_target=None, tick_target=None,
                layer_target=None, companion_target=None, hid_target=None, command_target=None, usb_reset_target=None, pwm_target=None) -> bytes:
    out = bytearray(data)
    for linked, pair in patch_sites(startup_target, tick_target, layer_target,
                                    companion_target, hid_target, command_target, usb_reset_target, pwm_target).items():
        want = pair["original"] if direction == "patch" else pair["extension"]
        got = _word(out, linked)
        if got != want:
            raise SystemExit(
                f"refused: word at {hex(linked)} is {got:08x}, expected {want:08x} "
                f"(already {'patched' if direction == 'reverse' else 'unpatched'}?)")
        out[linked - BASE:linked - BASE + 4] = (
            pair["extension"] if direction == "patch" else pair["original"]
        ).to_bytes(4, "little")
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=pathlib.Path, help="pinned V122 .bin")
    parser.add_argument("out", type=pathlib.Path, help="output .bin (patched or reversed)")
    parser.add_argument("manifest", type=pathlib.Path, help="JSON manifest to write")
    parser.add_argument("--reverse", action="store_true",
                         help="reverse the patch (output = original V122)")
    parser.add_argument("--startup-target", type=lambda s: int(s, 0))
    parser.add_argument("--tick-target", type=lambda s: int(s, 0))
    parser.add_argument("--layer-target", type=lambda s: int(s, 0))
    parser.add_argument("--companion-target", type=lambda s: int(s, 0))
    parser.add_argument("--hid-target", type=lambda s: int(s, 0))
    parser.add_argument("--command-target", type=lambda s: int(s, 0))
    parser.add_argument("--usb-reset-target", type=lambda s: int(s, 0))
    parser.add_argument("--pwm-target", type=lambda s: int(s, 0))
    args = parser.parse_args()

    data = args.image.read_bytes()
    # Forward patches start from the pinned V122 artifact. Reversal starts
    # from a patched artifact whose per-site words are verified instead.
    if not args.reverse:
        check_image(data)
    direction = "reverse" if args.reverse else "patch"
    targets = (args.startup_target, args.tick_target, args.layer_target,
               args.companion_target, args.hid_target, args.command_target, args.usb_reset_target, args.pwm_target)
    result = apply_patch(data, direction, *targets)

    args.out.write_bytes(result)
    manifest = {
        "tool": "stock_extension_patch.py",
        "direction": direction,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "result_sha256": hashlib.sha256(result).hexdigest(),
        "base": BASE,
        "sites": [
            {"linked": hex(linked), "file_offset": hex(linked - BASE),
             "original_word": f"{p['original']:08x}", "extension_word": f"{p['extension']:08x}",
             "applied_word": f"{(p['extension'] if direction == 'patch' else p['original']):08x}"}
            for linked, p in patch_sites(*targets).items()
        ],
        "oem_builder": hex(OEM_BUILDER),
        "extension_builder": hex(EXTENSION_BUILDER),
        "authorization": ("OFFLINE RESEARCH ARTIFACT. Not authorized for "
                          "installation; R3 gate unchanged."),
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{direction}: {len(result)} bytes, sha256 {manifest['result_sha256']}")
    for site in manifest["sites"]:
        print(f"  {site['linked']} (file {site['file_offset']}): {site['original_word']}"
              f" -> {site['applied_word']}" if direction == "patch" else
              f"  {site['linked']} (file {site['file_offset']}): {site['extension_word']}"
              f" -> {site['applied_word']}")
    print(f"builder pointer: {hex(OEM_BUILDER)} -> {hex(EXTENSION_BUILDER)}"
          if direction == "patch" else
          f"builder pointer: {hex(EXTENSION_BUILDER)} -> {hex(OEM_BUILDER)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
