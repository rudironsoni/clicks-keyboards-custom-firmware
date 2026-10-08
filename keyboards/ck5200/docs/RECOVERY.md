# Recovery for the iPhone 15 Pro Max case

**Recovery is not verified. Do not flash custom firmware yet.**

## Preserve the stock application without touching USB

From the repository root:

```sh
bash keyboards/ck5200/scripts/revert-stock.sh --download-only
```

This downloads or verifies the stock application and exits without opening USB. The source is `https://xinyi1.clicks.tech/CK-5200/iKeyboard_CK-5200_V122_120.bin`.

```text
size    18648 bytes
sha256  8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8
```

The ignored local copy is `.stock/iKeyboard_CK-5200_V122_120.bin`. This is an application image, not a full backup of the boot component, option bytes, or device-specific data.

## Stock still runs

The official Clicks app is the vendor's documented update path. This project's raw USB transport has not completed even a version exchange with the iPhone firmware. It refuses stock update commands before A1. Finding the expected endpoints does not establish a working updater.

`revert-stock.sh` without `--download-only` retains an explicit confirmation for an experimental restore through an already-running custom updater. That path is **NOT VERIFIED** and is not a rescue method for a device that does not boot.

## Custom firmware does not boot

No independent rescue path is proven for this case. `bootloader_jump()` currently resets the MCU; it does not enter a known recovery interface.

The following are required before the first custom flash:

1. Read the actual MCU marking and board revision.
2. Identify the accessible programming/debug or boot pins on this board, their voltage, and ground.
3. Check chip and protection state without erasing or changing option bytes.
4. Preserve the boot/install component and device-specific regions if readback is possible. If protection prevents readback, stop rather than disable protection, which may erase flash.
5. Verify the recovery tool, exact image addresses, and an actual stock recovery on suitable hardware with separate consent for the write.

The [WCH tool supports CH32V20x devices](https://www.wch-ic.com/downloads/WCHISPTool_Setup_exe.html). That vendor support does not identify the Clicks pads or prove access on this case. A generic CH32V203 development-board procedure is not a verified procedure for this keyboard.

Current blockers are the unconfirmed physical chip/pads, absent boot-component backup, unknown installer validation, and no tested independent recovery. No normal USB command in this project exposes full-flash readback.

## Update interruption remains unresolved

The custom staging writer latches write failure and requires a new session. It does not prove power-loss safety. The previous commit marker remains until the first new page is written, and the stock installer's behavior with an interrupted transfer has not been recovered. Do not claim rollback or atomic updates.

`tools/make_stock_probe.py` changes the stock version string. It is an experimental modified image, not a recovery tool. Flashing that probe still requires consent and the same recovery prerequisites.

## Hardware backup and restore guide, 2026-10-08

**Status: source-checked procedure, not tested on this keyboard. No full backup exists yet.** The target is Rudi's USB-C Clicks case for iPhone 15 Pro Max. Do not transfer another model's pad positions to this board.

### 1. Get the equipment

Recommended kit:

| Item | Purpose |
| --- | --- |
| WCH-LinkE, R0-1v3 or a documented compatible revision | WCH RISC-V debug connection. Use RISC-V mode, not ARM mode. |
| USB-C to USB-A female data adapter or hub | Connect the usual USB-A WCH-LinkE to the Mac. Match the actual probe connector when ordering. |
| Three short insulated leads, female 2.54 mm ends at the probe | Ground, debug data and debug clock. Add a fourth lead only if a verified reset connection becomes necessary. |
| Fine pogo probes in a stable holder, or fine test hooks that fit the pads | Contact the board without loose handheld needles. Measure pad spacing first. |
| Digital multimeter with fine probes | Find ground, trace pads to pins, and measure the board supply. |
| Magnifier or microscope and a camera | Read the full chip marking, pin-1 mark, board revision and pad labels. |
| Precision Phillips driver set and plastic opening tools | Open the case without guessing one exact screw size. |
| Insulating board holder and polyimide tape | Prevent shorts and strain on small pads. |
| Fine soldering tools, flux and insulated 30 AWG wire, only if fixed leads are needed | A technician can attach leads when hooks or pogo contacts cannot be secured. Do not lift chip pins. |
| A second storage device or private backup location | Keep a second copy of the verified dump and its metadata. |

The [Olimex WCH-LinkE listing](https://www.olimex.com/Products/RISC-V/WCH/WCH-LinkE/) lists CH32V20x support and was EUR 6.95 when checked, before any checkout charges. This is a source for the probe, not a guarantee that this keyboard permits readback. Do not buy an SPI flash clip, USB-UART cable or generic ARM-only ST-Link as a substitute for this procedure.

### 2. Open and identify the actual board

Remove the iPhone and all power before opening. Photograph each layer and retain the screw positions. Stop if hidden fasteners or a ribbon cable resist movement.

I inspected the board segment of [JerryRigEverything's Clicks teardown](https://www.youtube.com/watch?v=Qqp1ClBm-_I&t=480s). It shows a Lightning connector and `IP-KB_V2.3`, so it is **not a verified board match** for the iPhone 15 Pro Max. Its test-pad row cannot serve as this case's pinout. Exact opening steps, screw sizes and pad locations for Rudi's board remain [UNVERIFIED].

Before attaching the probe, record:

- Both sides of the actual board, its printed revision and every storage/controller chip marking.
- The full MCU part number, package and pin-1 mark. CH32V203 is still an inference until this is read.
- The pads connected to MCU ground, PA13 and PA14, with resistance measurements.
- The measured MCU supply voltage and how the board is powered.

If the chip is not a CH32V203-family part, stop and use its own documentation. Do not try the commands below against an unidentified chip.

### 3. Identify the debug pins

The [WCH-Link manual, table 6](https://www.olimex.com/Products/RISC-V/WCH/WCH-LinkE/resources/WCH-LinkUserManual.PDF) maps CH32V20x debug data to PA13 and clock to PA14. These names describe chip signals, not Clicks test-pad numbers.

Use the matching drawing in the [CH32V203 datasheet, printed pages 17-18](https://cdn-learn.adafruit.com/assets/assets/000/131/418/original/CH32V203DS0.PDF?1721655401=). Examples from those drawings:

| Exact part/package group | PA13/data pin | PA14/clock pin | NRST pin |
| --- | --- | --- | --- |
| CH32V203CxT6, LQFP48 | 34 | 37 | 7 |
| CH32V203CxU6, QFN48 | 34 | 37 | 7 |
| CH32V203KxT6, LQFP32 | 23 | 24 | 4 |
| CH32V203G6U6, QFN28 | 21 | 22 | 4 |
| CH32V203G8R6, QSOP28 | 28 | 1 | 8 |
| CH32V203F6P6, TSSOP20 | 19 | 20 | 4 |
| CH32V203F8P6, TSSOP20 | 1 | 2 | 5 |
| CH32V203F8U6, QFN20 | 16 | 17 | 0 is ground; no separate NRST shown |

The last two rows demonstrate why a near-matching part name is insufficient. Some packages share debug pins with USB signals. Confirm both the package and board routing. These are **conditional chip pin numbers**, not a claimed map of this keyboard.

With the board unpowered, measure continuity from accessible test pads to the identified pins. Use a known MCU VSS connection for ground. Do not assume the USB metal shield is signal ground. Record the resistance; a trace with a series resistor may not trigger the meter's buzzer. Adjacent pins must not be bridged. For QFN packages, use accessible traces or component pads rather than trying to reach pins under the package.

### 4. Wire and power the board

Connect by the labels on the actual probe, not by a guessed header order:

| WCH-LinkE label | Board connection |
| --- | --- |
| GND | Verified MCU ground pad |
| SWDIO/TMS | Verified PA13 pad |
| SWCLK/TCK | Verified PA14 pad |
| RST | Leave disconnected initially; use only after confirming NRST and a need for reset-assisted attachment |
| 3V3 | Leave disconnected when the board has its own power |
| 5V | Leave disconnected in this wiring scheme |
| RX, TX, TDI, TDO | Leave disconnected |

Engineering recommendation: keep leads about 10 cm where practical and secure the contacts. WCH specifies a 30 cm maximum and recommends reducing debug speed for unstable links.

Power the board through its verified normal input and measure the regulated MCU rail. Do not assume the lower charging socket powers the keyboard with no phone attached. Do not inject 3.3 V into an unidentified pad or connect two power sources. If the package shares debug and USB data pins, the USB host data lines must not contend with the probe; establish a power-only input arrangement before attachment. That arrangement cannot be specified exactly until the board routing is known.

The MCU and probe need a common ground and compatible signal levels. The probe's 3V3 pin is a power output, not a general voltage-sense pin. Switch probe mode with the target disconnected. Leave the board's boot-pin network intact; do not short a guessed BOOT pad.

### 5. Establish permission to read before dumping

A debug attachment can halt or reset execution even when it writes no flash. Do this only after the identity, wiring and power checks above. Capture the tool version, probe firmware, detected chip identity, reported memory map, protection state and every command result.

Stop if the tool asks to erase, unprotect, enable debug by changing option bytes, or program first. Those are not backup steps. A protected or disabled debug interface can make a nondestructive backup unavailable. A failed connection alone does not prove protection; first check power, contact, mode and chip selection.

The [WCH-LinkUtility manual, section 5.2](https://www.olimex.com/Products/RISC-V/WCH/WCH-LinkE/resources/WCH-LinkUserManual.PDF) separates chip-information and protection queries from **Read Chip Flash**. Use those read functions only. Its programming tutorial includes releasing read protection; that step must not be copied into a backup procedure.

### 6. Cover all storage, not just the application

[CORRECTION] The earlier `0x10000` dump example covered only the first 64 KiB. It must not be called a full backup. The datasheet distinguishes the fast program area from additional flash. Confirm these candidate ranges against the actual part and tool map:

| Region | Start | Length | Role |
| --- | --- | --- | --- |
| Combined main flash | `0x08000000` | `0x38000` (224 KiB) | Includes fast program storage and additional flash |
| System flash | `0x1fff8000` | `0x7000` (28 KiB) | WCH boot code, separate from Clicks boot/install code |
| Vendor bytes | `0x1ffff700` | `0x100` (256 bytes) | Factory data region |
| Option bytes | `0x1ffff800` | `0x80` (128 bytes) | Chip configuration/protection region |

These bounds come from the datasheet memory-map figure, printed page 6. A tool may expose only part of a region or use a different option-region extent. Resolve any discrepancy before reading or restoring. Never infer full coverage from the chip's advertised fast-flash size alone.

Within main flash, preserve the first `0x2000` bytes, the application at `0x08002000`, staging at `0x08008a00`, and configuration around `0x0800f600` and `0x0800f700`. Preserve the extra flash even if its purpose is unknown. The low address range beginning at zero aliases flash; it is not another independent copy.

RAM is temporary runtime state, not a persistent firmware backup. Other chips on the board may hold independent data. Identify those chips before claiming that an MCU dump covers the entire product. Do not assume factory identity or authentication data can be rewritten or cloned.

### 7. Prepare the Mac tools

Use an existing tool rather than a new dumper. The examples below target **probe-rs 0.32.0**, source revision `48f5e4d53c690a1d40c2454033c6f785b4f4f95c`. They are source-reviewed templates, not commands tested on this keyboard. They require an installed Rust toolchain with `cargo`.

Install on the Mac with the keyboard/probe disconnected:

```sh
cargo install --git https://github.com/probe-rs/probe-rs --rev 48f5e4d53c690a1d40c2454033c6f785b4f4f95c --locked probe-rs-tools
probe-rs --version
probe-rs read --help
probe-rs download --help
```

Keep the installation log and exact version. Do not replace the pinned tool with a later version without checking its read, protection and restore behaviour.

After the wiring and power checks, enumerate the probe and identify the target:

```sh
unset PROBE_RS_ALLOW_ERASE_ALL PROBE_RS_CYCLE_POWER PROBE_RS_CONNECT_UNDER_RESET
unset PROBE_RS_DRY_RUN PROBE_RS_CHIP_DESCRIPTION_PATH PROBE_RS_PREFER_FLASH_ALGO
unset PROBE_RS_CHIP PROBE_RS_PROBE PROBE_RS_PROTOCOL PROBE_RS_SPEED
probe-rs list
probe-rs info
```

Those variables can override default protection, reset, target-description or simulation behaviour. The [CLI options](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs-tools/src/bin/probe-rs/util/common_options.rs) grant erase-all permission only when requested. Do not add `--allow-erase-all`, `--chip-erase`, `--connect-under-reset` or `--cycle-power` to these backup steps.

If more than one probe appears, select the intended one using the tool's `--probe` argument. Do not allow an automatic choice between multiple targets. Identity detection does not replace reading the physical chip marking.

Only if both identification methods confirm **CH32V203C8T6**, use this example selection:

```sh
CK_CHIP=CH32V203C8T6
probe-rs chip info "$CK_CHIP"
```

For another marking, select that exact supported part and check its memory map first. Do not use the example just because the firmware repository says CH32V203.

For a confirmed CH32V203 matching the SDK register map, these are read-only protection-register checks:

```sh
probe-rs read --chip "$CK_CHIP" b32 0x4002201c 1
probe-rs read --chip "$CK_CHIP" b32 0x40022020 1
```

The first value is `FLASH->OBR`; bit `0x2` set means read protection is active. The second is `FLASH->WPR`; retain it and check write-protected pages before planning a restore. If it differs from `0xffffffff`, do not assume the whole flash is writable. If either read fails, do not change protection to force access. These addresses follow the [WCH SDK register structure](https://github.com/openwch/ch32v20x/blob/baef0054588f4826548429ff4e7a9257d752ef2f/EVT/EXAM/SRC/Peripheral/inc/ch32v20x.h) and [read-protection test](https://github.com/openwch/ch32v20x/blob/baef0054588f4826548429ff4e7a9257d752ef2f/EVT/EXAM/SRC/Peripheral/src/ch32v20x_flash.c).

### 8. Extract two copies

**Prerequisites:** confirmed part and ranges, readable/unprotected device, stable power, secured contacts, and no unresolved request to unlock or erase. For the following template, the four ranges in section 6 must match the confirmed chip. Use an empty directory outside Git. These commands do not request programming.

```sh
CK_BACKUP="$HOME/Desktop/ck5200-stock-backup-2026-10-08"
mkdir "$CK_BACKUP"
probe-rs --version > "$CK_BACKUP/tool-version.txt"
probe-rs chip info "$CK_CHIP" > "$CK_BACKUP/chip-map.txt"

for CK_PASS in 1 2; do
  probe-rs read --chip "$CK_CHIP" b8 0x08000000 229376 \
    --output "$CK_BACKUP/main-$CK_PASS.bin" --format binary || break
  probe-rs read --chip "$CK_CHIP" b8 0x1fff8000 28672 \
    --output "$CK_BACKUP/system-$CK_PASS.bin" --format binary || break
  probe-rs read --chip "$CK_CHIP" b8 0x1ffff700 256 \
    --output "$CK_BACKUP/vendor-$CK_PASS.bin" --format binary || break
  probe-rs read --chip "$CK_CHIP" b8 0x1ffff800 128 \
    --output "$CK_BACKUP/options-$CK_PASS.bin" --format binary || break
done
```

Save the terminal output with the backup. Stop at any failed read. `b8` makes the count a number of bytes. `--format binary` is essential; the default is formatted hex text. The [read implementation](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs-tools/src/bin/probe-rs/cmd/read.rs) resumes cores after a successful read. This sequence is not a frozen, simultaneous snapshot. Leave the keyboard idle; investigate differing reads instead of discarding one. If a stable snapshot requires continuous halt, use a reviewed persistent debug session, not an assumed flag on this command.

A successful file write is not proof of a valid memory read. WCH tooling can return invalid/protected data. Known invalid-read patterns and implausibly blank application data must be investigated. CH32V20x erased areas can contain `39 e3` repeats, so an unused area's pattern is not by itself corruption. See [wlink's CH32V20x notes](https://github.com/ch32-rs/wlink/blob/401a68a63b3266ad74a75aee2748ebadd6cfa5ff/docs/CH32V20x.md).

### 9. Check and preserve the backup

Run this host-only check after all eight files exist. It checks exact lengths, matching reads, and hashes. It does not contact the keyboard.

```sh
python3 - "$CK_BACKUP" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
regions = {
    "main": (0x08000000, 0x38000),
    "system": (0x1fff8000, 0x7000),
    "vendor": (0x1ffff700, 0x100),
    "options": (0x1ffff800, 0x80),
}
manifest = {}
for name, (address, size) in regions.items():
    first = (root / f"{name}-1.bin").read_bytes()
    second = (root / f"{name}-2.bin").read_bytes()
    if len(first) != size or len(second) != size or first != second:
        raise SystemExit(f"STOP: length or repeat-read mismatch for {name}")
    if first.startswith(bytes.fromhex("a9 bd f9 f3")):
        raise SystemExit(f"STOP: known invalid-read prefix for {name}")
    manifest[name] = {
        "address": hex(address), "size": size,
        "sha256": hashlib.sha256(first).hexdigest(),
    }
main = (root / "main-1.bin").read_bytes()
application = main[0x2000:0x68d8]
app_hash = hashlib.sha256(application).hexdigest()
expected = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
print("Official CK-5200 V122 application match:", app_hash == expected)
print("Raw staging marker:", main[0x8a00:0x8a04].hex())
print(json.dumps(manifest, indent=2))
(root / "readback-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
PY
```

A false application match requires investigation. It may indicate a different model/release, wrong byte order, an invalid read, or changed contents. Never overwrite the original dump with the vendor download. Matching files can both be wrong, so keep the chip/protection logs and inspect the contents as well.

Record the raw staging marker and determine whether it requests an install on restart. Its meaning is not fully established here. Preserve both raw backups; do not edit either to clear a marker. Keep photos, exact chip/board/probe details, voltage, wiring, tool revision, addresses, logs and hashes together. Copy the complete folder to a second private storage location. Label it **readback captured, restoration unverified** until a restore test succeeds.

### 10. Restore stock after a failed custom image

**This section writes flash. It is documentation, not consent to run it.** The commands remain untested on this keyboard. The prerequisite is a validated backup from this same board, a confirmed chip/map, successful debug access, stable power and a separate decision to restore.

First inspect protection again and compare the current system, vendor and option regions with the saved originals. If any changed, stop the ordinary main-flash procedure. Do not use an unlock to regain access or a full-chip erase to simplify recovery. Confirm that the saved staging state cannot install a different image at the next restart.

For confirmed CH32V203C8T6 with unchanged factory/configuration regions and a complete 229,376-byte main-flash backup, the source-checked command is:

```sh
probe-rs download --chip "$CK_CHIP" --binary-format bin \
  --base-address 0x08000000 --verify "$CK_BACKUP/main-1.bin"
```

This restores the saved main flash, including the inferred Clicks boot/install area, stock application, staging/configuration and additional storage. It is **not** the same as flashing the vendor's small application BIN. Never write that application BIN at `0x08000000`; its inferred application address is `0x08002000`.

The [download path](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs-tools/src/bin/probe-rs/cmd/download.rs) uses the normal flash loader. [Its options](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs-tools/src/bin/probe-rs/util/cli.rs) erase affected pages/sectors and request comparison with `--verify`. Omitting `--chip-erase` avoids requesting a whole-chip erase; it does not make programming erase-free. Omitting `--reset` avoids that explicit post-download reset, but does not guarantee that attachment, the flash algorithm or session cleanup leaves execution untouched.

No code-protection-clear step was found in the audited CLI/target definition. The embedded flash algorithm has not been independently proved against this keyboard. A read-protected or write-protected target is therefore a stop condition, not permission to add erase/unlock flags.

After successful programming and its verification, independently read main flash and compare it:

```sh
probe-rs read --chip "$CK_CHIP" b8 0x08000000 229376 \
  --output "$CK_BACKUP/main-after-restore.bin" --format binary
cmp "$CK_BACKUP/main-1.bin" "$CK_BACKUP/main-after-restore.bin"
```

`cmp` must exit 0. Retain any mismatch and the log; do not reset and hope. Remember that `probe-rs read` resumes cores. If no execution may occur before all verification completes, these separate CLI commands are insufficient; use a reviewed persistent debug session that holds the core stopped. The keyboard's precise halt/resume and watchdog behaviour remains [UNVERIFIED].

Re-read system/vendor/options and compare each with its original before accepting recovery. Do not rely on the exit code of the separate `probe-rs verify` command: [v0.32.0 prints a mismatch and then returns success](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs-tools/src/bin/probe-rs/cmd/verify.rs). Actual readback equality and the programming verification result are required.

If main flash and the preserved regions match, perform one controlled cold start. Remove power before removing debug leads. Restore the normal connectors and power path. Check enumeration first, then physical keys, modifiers, special keys, backlight, sleep/wake, charging and companion-app behaviour on the **iPhone 15 Pro Max**. Desktop typing alone does not establish recovery.

#### If system flash or option bytes were changed

The [v0.32.0 CH32V203C8T6 target definition](https://github.com/probe-rs/probe-rs/blob/v0.32.0/probe-rs/targets/CH32V2_CH32V3_Series.yaml) has separate main, system and option flash algorithms. It marks the vendor region as not writable. This is why a blanket "write every file back" command is wrong.

A separate repair plan must identify the changed region, check its erase size, and restore only the saved bytes from this chip. Option settings can affect read protection, debug access and boot behaviour; restoring them can remove the connection needed to verify other regions. Restore such settings only after all other repair and verification, with their decoded effects understood. Do not write vendor identity/calibration data. If those bytes or an external authentication/storage chip cannot be restored, a main-flash backup alone cannot guarantee complete recovery. No unconditional system/option write command is provided because the actual chip, original settings and damage are still unknown.

### 11. Stop conditions and verification limits

- No readable full chip marking or no confirmed ground/data/clock pads: no connection recipe for this board yet.
- Read protection, disabled debug, an erase/unprotect prompt, or invalid data: stop before writes.
- Only a 64 KiB file captured: incomplete coverage until additional flash and the other regions are accounted for.
- Different repeated reads or a failed stock-image comparison: keep the evidence and diagnose it.
- Unidentified external storage or unreadable factory regions: record the backup as partial.
- Restore writes fail or verification differs: retain debug access and investigate before another reset or erase.

This guide was checked against WCH documentation, the inspected teardown and pinned tool source. All nine shell blocks passed syntax checks. The host-only backup checker passed a matching synthetic fixture and rejected a mismatched fixture. Existing tests passed: five Python tests and the C protocol test. The extraction commands were not run, the pinned tool was not installed here, and no board was opened or probed. A firmware rebuild was skipped because this turn changes documentation only. Exact Clicks pad positions, physical part number, power routing, protection state and successful restoration remain [UNVERIFIED].
