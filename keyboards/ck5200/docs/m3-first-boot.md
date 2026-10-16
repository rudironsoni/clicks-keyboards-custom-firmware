# M3 PR1: firmware first-boot hardening

Date: 2026-10-16. Starting revision: `246f2b8`, branch `feat/m3-first-usable-qmk`.

**Offline checks pass. Custom flashing remains NO-GO. R3 is unmet.**
M1 and M2 remain accepted. This work does not establish recovery independent of
the application. No hardware access, flash operation, ISP/SDI operation,
protection change, or device reset was performed. Reset and flash calls in tests
run only against host models. Existing baseline evidence is unchanged.

## Verified faults and fixes

1. **USB configuration parsing:** the old array had 97 bytes but declared 96.
   An extra zero followed the two-byte HID report length. Removing it restores
   descriptor boundaries. The compiled C test walks every descriptor and compares
   all 96 bytes with V122, allowing only HID version 1.11 versus stock 1.01 and
   the custom report length.
2. **HID field definitions:** the key array inherited the modifier usage range.
   The consumer field had no usage range, used Variable instead of Array, and
   capped the logical value at 511 although the keymap emits `0x029D` (669).
   The descriptor now defines 6KRO key usages, the five LED output bits, and a
   separate Consumer Control collection with one 16-bit usage selector. Report
   protocol is 10 bytes; boot protocol is eight bytes, including GET_REPORT.
3. **Device identity:** USB used `190200001`; Identify used `2311000001`.
   Neither constant is the device's runtime serial. Stock `0x2276..0x22d8`
   reads one word at `0x0800F600` and formats ten decimal digits. USB index 3
   calls this at `0x3a78..0x3a88`; Identify calls it at `0x57a0` and includes
   the trailing NUL, for 11 value bytes. Both custom paths now use that same
   read-only word and format. No fallback identity is invented. Zero or erased
   words retain stock's decimal formatting. Host values are synthetic fixtures,
   not observed serial numbers. No serial/configuration page is written.
4. **Identify parameter length:** stock `0x58ae` loads `0x0b` (11) for parameter
   `0x0b`, the `79Q5CGN6JK` string plus NUL. The old extraction used 17 and
   included six bytes of the following data. The generator and generated header
   now use 11. There are **17 parameters**, not the 16 stated in the old notes.
   The stock firmware/hardware strings `1.2.2`/`1.2.0` remain compatibility
   fields. Custom routing remains USB `bcdDevice=0x9001` and dispatcher `QM`;
   the iAP2 firmware string alone must not be used to identify stock firmware.
5. **Matrix decode:** the old audit began at `0x2b1c` and missed the preceding
   modifier builder. Crossing `(2,3)` is Shift, not a spare. Stock also adds
   Shift to eight fixed SYM usages. These outputs now match the instructions
   at `0x2a86..0x2ab0`. Consumer crossing `(0,5)` sets report bit 2, which the
   stock descriptor maps to `0x00CF`, not `0x01AE`. Its mapping is corrected.
6. **Report loss:** the new production wire test failed all eight shifted SYM
   cases. QMK can emit modifier-down, key-down, key-up, modifier-up within one
   scan. The old single pending slot replaced the key-down state with the final
   release. A 16-entry queue now retains complete 10-byte snapshots. A failed
   transfer keeps the head for retry. Queue overload retains the first pending
   states and replaces the last slot with the latest state, preserving the final
   release. This is bounded storage, not an unlimited macro transport.
7. **USB recovery:** suspended/unmounted typing coalesces to current state.
   Resume/mount resends that state. New mounts reset boot/report protocol, idle
   rate, and LED state. Rejected consumer submissions remain queued. Idle repeat
   and the existing completion handling remain covered.
8. **Auth backpressure:** rejecting the first certificate packet produced the
   second chunk instead of the first in the production iAP2 test. `op_pump`
   advanced its cursor before USB accepted the packet. Cursor advancement now
   follows successful submission. The byte-exact certificate test covers both
   first-packet and final-packet rejection, with retries and sequence checks.

## Boot and transport inspection

- The selected entry is project `firmware/platform/ch32x035/main.c`, not merely
  QMK's weak main. Normal ordering matches pinned `quantum/main.c`: platform,
  protocol, keyboard setup, pre-init, keyboard init, post-init, then the task
  loop and housekeeping. The existing M2 harness executes this actual entry
  and proves normal QMK setup/tasks with zero modeled flash writes.
- The WCH reset assembly initializes `gp`/stack, copies `.data`, clears only
  `.bss`, sets `mtvec`, calls `SystemInit`, and enters main with `mret`.
  `.noinit` remains an eight-byte NOLOAD region outside the BSS clear and stack.
  The ELF validator checks those linked bounds against the binary.
- `platform.c` uses the 48 MHz HSI, a 1 kHz SysTick, USBFS AHB clock, PC16
  floating input, PC17 pull-up, and the AFIO USB PHY bits. These match the pinned
  TinyUSB X035 BSP initialization. USBPD is disabled. USB and wake IRQ handlers
  both call TinyUSB root port 0. Actual enumeration timing remains untested.
- Production matrix scanning drives PB0..PB5 one at a time and reads PA0..PA5.
  V122 `0x24aa..0x2580` reads `0x40010808 & 0x3f`, stores the sample by row
  index, and selects the next PB bit through `0x40010c10`. Stock initialization
  at `0x25bc..0x2644` sets PA pull-down inputs and PB outputs. This supports the
  electrical index interpretation. It does not prove keycap labels or electrical
  timing. Custom settling is 2 microseconds per row; stock uses a tick-driven
  scanner. The custom output-speed choice and settle time need physical checks.
- `iap2_auth.c` retains the stock PA13/PA14 bit-banged bus, address fallback,
  challenge relay, and bounded retry behavior. Its waveform tests pass. Pull-ups,
  chip timing, phone acceptance, and multi-packet USB behavior are not proved by
  the host model. No authentication storage or stock configuration is changed.
- Endpoint topology stays HID interface 2 (`81/01`), iAP2 interface 0 (`82/02`),
  and EA interface 1 (`83/03`, alternate 1). Tests model TinyUSB completion and
  failure callbacks, not a real USB controller or iPhone.

## All 36 electrical crossings

`r,c` means PB row index and PA input bit. `S(x)` means the left-Shift modifier
plus keyboard usage x. Punctuation names below assume a US host layout; the
usage and modifier values, not a host's character rendering, are the evidence.
`=` means the custom output follows the listed stock fixed usage. `base` means
transparent fallback to custom layer 0. Stock cursor mode is a runtime flag;
custom navigation is the existing held layer on `(5,2)`.

**[UNVERIFIED] Every physical cap-to-crossing assignment remains unconfirmed.**
Letter names identify decoded HID output, not photographed cap locations.
The C harness exercises all 36 crossings through the production scanner,
debounce, QMK keymap/actions, protocol queue, and report submission.

| r,c (mask) | V122 base / SYM / cursor | Custom L0 / L1 / L2 | Stock code |
| --- | --- | --- | --- |
| 0,0 (`01`) | Q / 1 / Q | = / = / base | `2b76..2b8a` |
| 0,1 (`02`) | U / 7 / U | = / = / base | `2b8e..2ba6` |
| 0,2 (`04`) | K / quote / Down | = / = / Down | `2baa..2bc6`, `2cce..2cd8` |
| 0,3 (`08`) | Backspace | = / base / base | `2bca..2bdc` |
| 0,4 (`10`) | Enter, config-dependent Shift interaction | Enter / base / base | `2be0..2c0c`, `2cda` |
| 0,5 (`20`) | consumer bit 2 = `00CF` | `00CF` / base / base | `2c10..2c1e` |
| 1,0 (`01`) | W / 2 / Up | = / = / Up | `2c24..2c3c`, `2ce0..2ce6` |
| 1,1 (`02`) | I / 8 / Up | = / = / Up | `2c40..2c5a`, `2ce8..2cf0` |
| 1,2 (`04`) | D / S(semicolon) / Right | = / = / Right | `2c5e..2c78`, `2cf2..2cfa` |
| 1,3 (`08`) | L / S(quote) / Right | = / = / Right | `2c7c..2c96`, `2cfc..2d04` |
| 1,4 (`10`) | V / S(slash) / V | = / = / base | `2c9a..2cb2` |
| 1,5 (`20`) | consumer bit 1 = `029D`; chord with 5,4 changes backlight | `029D` / base / base; no stock chord | `2cb6..2ccc`, `2d06..2d0c`, `2900` |
| 2,0 (`01`) | E / 3 / E | = / = / base | `2d12..2d28` |
| 2,1 (`02`) | O / 9 / O | = / = / base | `2d2c..2d44` |
| 2,2 (`04`) | F / semicolon / F | = / = / base | `2d48..2d60` |
| 2,3 (`08`) | Shift, including configurable sticky handling | Left Shift / base / base | `2958..29be`, `2ae6..2b1a`, `2b6a` |
| 2,4 (`10`) | B / S(1) / B | = / = / base | `2d64..2d7a` |
| 2,5 (`20`) | Left GUI | = / base / base | `2d7e..2d8c` |
| 3,0 (`01`) | R / 4 / R | = / = / base | `2d92..2da8` |
| 3,1 (`02`) | P / 0 / P | = / = / base | `2dac..2dc4` |
| 3,2 (`04`) | G / S(9) / G | = / = / base | `2dc8..2de0` |
| 3,3 (`08`) | Z / config-dependent usage and modifiers | Z / base / base | `2de4..2e08`, `2f4e` |
| 3,4 (`10`) | N / comma / N | = / = / base | `2e0c..2e24` |
| 3,5 (`20`) | Space, suppressed while 5,4 is held | Space / base / base | `2e28..2e42` |
| 4,0 (`01`) | T / 5 / T | = / = / base | `2e4a..2e60` |
| 4,1 (`02`) | A / minus / Left | = / = / Left | `2e64..2e7e`, `2f56..2f5e` |
| 4,2 (`04`) | H / S(0) / H | = / = / base | `2e82..2e9a` |
| 4,3 (`08`) | X / S(7) / X | = / = / base | `2e9e..2eb6` |
| 4,4 (`10`) | M / period / M | = / = / base | `2eba..2ed2` |
| 4,5 (`20`) | Ctrl if config `+120 & 40`, otherwise Tab | fixed Left Ctrl / base / base | `2ed6..2eee`, `2f60..2f70` |
| 5,0 (`01`) | Y / 6 / Y | = / = / base | `2ef0..2f0c` |
| 5,1 (`02`) | S / slash / Down | = / = / Down | `2f10..2f2a`, `2f72..2f7a` |
| 5,2 (`04`) | J / config-dependent usage/modifiers / Left | tap J, hold L2 / base / layer control | `2f2e..2f4c`, `2f7c..2fd0`, `3000..300e` |
| 5,3 (`08`) | C / S(2) / C | = / = / base | `2fd4..2fe8` |
| 5,4 (`10`) | SYM selection and configurable sticky/chord handling | existing OSL(1) / base / base | `2958..29be`, `2a54..2ae4` |
| 5,5 (`20`) | consumer bit 0 = `01AE` | existing Eject `00B8` / base / base | `2fec..2ffe` |

The consumer-bit ordering is literal in the 97-byte descriptor at `0x6580`:
Usage `01AE`, Usage `029D`, Usage `CF`, Report Size 1, Report Count 3.
Shifted SYM selection comes from `0x2a86..0x2ab0`; the later letter builder
alone does not describe the full report. The old extraction script reports
only 34 cases and follows some cursor branches instead of base branches.
Its output is not an exhaustive oracle.

[CORRECTION] The old "spare crossing" claim and unshifted D/L/V/B/G/H/X/C
SYM claims are incomplete. The table above includes the missing pre-builder.
The existing custom Clicks hold layer, Eject substitution, fixed Ctrl choice,
and simplified sticky behavior are deliberate differences, not stock parity.
Stock config-dependent Z/J outputs, Enter behavior, and backlight chords are
not reimplemented. The firmware does not read or alter the config page to
guess the user's preferences. Layer 3 remains transparent; edits remain RAM-only.

## Independent input references

Stock V122: `.stock/iKeyboard_CK-5200_V122_120.bin`, 18,648 bytes,
SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
Addresses above are linked addresses, file offset plus `0x2000`. The image hash,
descriptor bytes, serial code, pre-builder, letter builder, and Identify calls
were inspected, rather than accepting the previous table as ground truth.

The independent official app input is the root archive
`com.clickscompanion.app_3.0.2_und3fined.ipa.ipa`:

- Archive SHA-256: `c4fdcc9050997ed20035f668c8eadbff51764f5c07e14e63aa2bb744b2b9b33c`.
- Zip member: `Payload/Clicks.app/Clicks`, 9,771,792 bytes.
- Executable SHA-256: `d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b`.
- Python `zipfile` and `struct` verified Mach-O `LC_ENCRYPTION_INFO_64`:
  crypt offset 16,384, size 7,766,016, **cryptid 0**.
- `__TEXT` VM base `0x100000000`, file offset 0, size 7,798,784.

| VM address | File offset | Little-endian bytes | Instruction word | CMP immediate |
| --- | --- | --- | --- | --- |
| `100179dd4` | `179dd4` | `ff 86 02 71` | `710286ff` | `A1` |
| `10017a0dc` | `17a0dc` | `1f 89 02 71` | `7102891f` | `A2` |
| `10017a4c4` | `17a4c4` | `ff 8e 02 71` | `71028eff` | `A3` |
| `10017a6c0` | `17a6c0` | `ff 82 02 71` | `710282ff` | `A0` |

These bytes verify the reported ACK-family comparison constants. They do not
prove the full outgoing frame format, chunk size, device-side install behavior,
or recovery. No protocol or safety decision was broadened from these constants.
The input archives and extraction directories remain untracked and untouched.

## Checks, artifacts, and limits

Commands run from `keyboards/ck5200`:

```sh
bash scripts/check-build.sh
bash scripts/test.sh
.venv/bin/python tools/validate_image.py build/ck5200_qmk.bin --elf build/ck5200_qmk.elf
```

All pass. The test script preserves the existing updater, staging/restore,
power-loss, auth waveform, QMK core, M2 rescue/watchdog, and wrong-family tests.
Python tests: 5 protocol, 2 layout, 13 image checks. Production C coverage adds
descriptor interpretation, all 36 scanner crossings, Shift/GUI/Ctrl chords,
one-shot SYM and return to base, all eight shifted SYM outputs, seven held-layer
arrows, all three consumer usages, 20 rapid taps with all 40 wire edges, failed
reports, suspend/resume, disconnect/reconnect, boot protocol, and bounded queue
overload. QMK core/protocol/M2 harnesses use ASan and UBSan. The certificate test
includes busy-endpoint retry for both chunks. Existing 200,000-packet dispatcher
fuzz coverage also passes.

Image validation now rejects unknown nonempty allocated sections, executable
RAM, writable code, installer overlap, overflow, and unexpected linked flash
SDK/persistent EEPROM functions. A disassembly check permits flash-controller
base loads only in `staging_write_page` and `SystemInit` (flash latency setup).
This is a pinned-compiler regression check, not proof against arbitrary C
pointer arithmetic or every possible machine-code address construction. Existing
staging tests check all writes stay in `08008A00..0800F4FF`; normal entry has
zero writes. No new persistent storage or write path was added.

Compiler: xPack GNU RISC-V Embedded GCC arm64 15.2.0, release `15.2.0-1`.
Dependency revisions are checked by `scripts/build.sh` against `scripts/pins.env`.
No dependency was modified. Two clean builds match byte-for-byte for BIN and
ELF. Raw linker maps contain random GCC temporary names; the script separately
compares `ck5200_qmk.map.normalized`, replacing only the root path and LTO
temporary prefix. It retains the raw map and hashes both. CI now performs this
same two-build comparison and uploads both maps plus `SHA256SUMS`.

| Resource | Measured |
| --- | --- |
| Binary / flash load | 26,188 bytes (`0x664c`) of 27,136 |
| Growth from starting image | 176 bytes (starting image 26,012) |
| Flash headroom | 948 bytes; image ends at `0x864c`, before staging at `0x8a00` |
| `.init` / `.vector` / `.text` | 4 / 252 / 25,788 bytes |
| `.data` / `.bss` / `.noinit` | 144 / 3,988 / 8 bytes |
| Static RAM before stack reservation | 4,140 bytes |
| Stack reservation plus alignment | 2,052 bytes (2,048-byte configured stack) |
| Total linked RAM reservation | 6,192 of 20,480 bytes; 14,288 bytes unreserved |
| Reset jump | `0x5e50506f`, target `0x7de4` |

SHA-256 of the final local artifacts under `build/`:

| Artifact | SHA-256 |
| --- | --- |
| `ck5200_qmk.bin` | `d48a921c50d5af16b080aaf6d270e7c4a8a1e0a7f7a0cafbba1591e24601e316` |
| `ck5200_qmk.elf` | `124f2d589e8ad77c496631062ad202b24ba3e1efa4613f5b52258658229b1916` |
| `ck5200_qmk.map` | `fef9f8aa56656b49611f37a431131cf885b272c9d4974ba0c78acd507341be7f` |
| `ck5200_qmk.map.normalized` | `d013d0fdfed66ce4b19bdafc0356b1f6ebef6bddd38f3477e9f9371032610af9` |

Artifacts and logs remain ignored. Raw map hashes vary with GCC temporary names;
the normalized map is reproducible in the checked two-build run. Cross-host
reproducibility and a remote CI run have not been verified. Existing upstream
compiler warnings remain (QMK declarations/host pointer casts, TinyUSB MCU
macro/deprecated `tud_init`, and discarded newlib syscall stubs).

## Remaining hardware gates

- R3: independent recovery after application failure remains unproved.
- Stock installer's acceptance and copy bounds, interrupted-install recovery,
  and complete device-specific storage preservation remain unproved.
- Physical keycap correspondence, scan settle time, ghosting, USB enumeration,
  iPhone HID/consumer interpretation, auth timing, EA reconnect, and actual
  power/suspend behavior need hardware evidence after the recovery gate is met.
- Six-key rollover is the implemented report format. The 16-entry queue covers
  the tested scans; overload coalesces its last state. Global debounce still
  suppresses sub-5-ms taps and can delay other keys during sustained chatter.
- RAM figures are link allocations, not measured stack high-water usage.

Hardware checks were intentionally skipped. No Xcode, iOS build, flashing,
commit, or push was performed by this firmware stream.
