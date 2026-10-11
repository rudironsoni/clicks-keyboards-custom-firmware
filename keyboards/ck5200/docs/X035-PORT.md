# CH32X035 port report, 2026-10-12

Status of the re-target ordered in the 2026-10-12 review: the firmware is
built for the CH32X035 family the stock firmware proves, with offline
evidence for every claim. Nothing in this work touched the physical
CK-5200: no flash, no erase, no ISP entry, no electrical manipulation.

## Reproducible build

```sh
bash keyboards/ck5200/scripts/build.sh
```

Dependency revisions (verified by build.sh against the checkouts):

| Dependency | Repository | Revision |
| --- | --- | --- |
| QMK core | qmk/qmk_firmware | `3d4da6de29c8635c9cd232ce456d8fec8d31921b` (0.26.11) |
| TinyUSB | hathach/tinyusb | `efa3e132235aefa4ef49d31c746c0c5f0c0e0bc9` (0.21.0) |
| WCH CH32X035 SDK | openwch/ch32x035 | `c6253942b29ff2a3f5e2829182a874afffa45881` |
| Toolchain | xpack-riscv-none-elf-gcc | 15.2.0-1 (rv32imac_zicsr_zifencei, ilp32) |

## Artifacts

```text
file     build/ck5200_qmk.bin
size     24136 / 0x5e48 (max 0x6a00, region 0x2000..0x8a00, 88.94%)
sha256   249eb262a9d743a282227ea2e48d178299a02f239bf8a93fd3c9049313964ea6
entry    0x2000 (reset JAL -> 0x7704)
RAM      6016 B used of 20 KB (29.38%); stack 2 KB at top
```

The app bundle carries the same bytes
(`ios/ClicksInspector/Resources/ck5200_qmk.bin`); the app still refuses to
stage anything but the authorized stock artifact (see "Authorization
distinction" below).

## Replaced CH32V203 assumptions, with their X035 evidence

| Area | Now | Evidence |
| --- | --- | --- |
| Startup/vector | `startup_ch32x035.S` from the pinned SDK; `mtvec = _vector_base \| 3` | SDK Startup dir; same convention the stock image uses (reviewer 2) |
| USB port | `tud_init(0)` / `tud_int_handler(0)`; IRQ 45 = `USBFS_IRQHandler` at vector slot 45 | SDK `ch32x035.h:72` `USBFS_IRQn = 45`; BSP `family.c` uses port 0 |
| USB pins | PC16 (D+) / PC17 (D-) configured in `platform_setup`, PHY enabled via `AFIO->CTLR` (`USB_PHY_V33 \| UDP_PUE \| USB_IOEN`), USBPD block powered down | BSP `family.c:81-94`; stock 0x41d0/0x3b04 |
| Clock | 48 MHz HSI, no PLL; `SystemCoreClock` fixed by the BSP system file; family gate asserts `HSI_VALUE == 48000000` | SDK `ch32x035.h:22`; stock 0x3bba/0x3bfc |
| Flash | 256-byte pages, fast-mode programming (`FLASH_BufReset`/`FLASH_BufLoad`/`FLASH_ProgramPage_Fast`); SDK bounds code flash at `FLASH_BASE + 0xF800` | SDK `ch32x035_flash.c:59-60,607-760`; reference manual ch. 20 |
| GPIO | `GPIO_Speed_50MHz` (the only speed enum); no open-drain output mode exists, so the OD hook maps to push-pull (the iAP2 bit-bang releases SDA by switching to pull-up input, which is what stock does at 0x5090) | SDK `ch32x035_gpio.h:25,26-35` |
| Persistence | ALL flash-backed persistence disabled: keymap lives in QMK's transient (RAM) EEPROM; edits are runtime-only and lost on power loss | review requirement 5; no writable region exists (see memory map) |
| Linker | Unchanged layout: `0x2000` base, `0x6A00` limit, 20 KB RAM, 2 KB stack; the application-limit assert still runs | `firmware/platform/ch32x035/linker.ld` |

## Inspection results (from the built ELF)

Verified with `llvm-objdump` on `build/ck5200_qmk.elf`:

- USB peripheral: 19 references to `0x40023xxx`, zero to `0x50000xxx`. The
  V203 image's fatal address is gone.
- Clock init: `SystemInit` sets HSION only; no PLLON (bit 24) write exists
  anywhere in the image.
- Vector table (absolute-entry mode, base `0x2004`): slot 45 = `0x6b88`
  = `USBFS_IRQHandler` (a real handler), slot 46 = `USBFSWakeUp_IRQHandler`;
  the V203's slot 59 is `0x00000000`, exactly as stock's is.
- Staging: `marker_write` computes its base as `0x08008A00` (`lui 0x8009;
  addi -0x600`).
- `validate_image.py` (full ELF/BIN validation): entry `0x2000`, `.vector`
  matches `_vector_base`, all sections inside the application region, RAM
  bounds hold. The boot region (`0x0..0x2000`) contains no loadable data.

## Flash-region protection (three layers)

1. Compile time: `firmware/platform/ch32x035/family_gate.c` (linked into the
   firmware) asserts `USBFS_BASE == 0x40023400`, `USBFS_IRQn == 45`,
   `HSI_VALUE == 48000000`, rejects `SYSCLK_FREQ_144MHz_HSI` outright, and
   asserts the staging limit stays below the documented code-flash end
   (`0x0800F7FF`; SDK `ValidAddrEnd = FLASH_BASE + 0xF800`).
2. Link time: the linker's application-limit assert; the validator's
   region checks (no loadable byte outside `0x2000..0x8A00` plus RAM).
3. Runtime: `ck5200_staging_page_in_range()` admits only aligned 256-byte
   pages fully inside `[0x08008A00, 0x0800F500)`. The restore harness
   unit-tests every boundary: installer `0x08000000` and application
   `0x08002000` rejected; staging first/last page accepted; `0x0800F500`,
   serial `0x0800F600`, config `0x0800F700`, and reserved `0x0800F800`
   all rejected; unaligned addresses rejected.

## X035 flash staging with fault injection

The staging writer now models the real X035 constraints: every write is a
page erase followed by a buffer-loaded page program, and both length-marker
operations (A1 invalidation, A3 commit) are page read-modify-write
transactions, because the marker shares its page with the first 252 staged
bytes and the SDK exposes no word programming.

The restore harness (`tests/ck5200_restore_test.c`, host model with
erase-then-AND program semantics) covers:

- Full cycles both directions with the real stock and custom images,
  byte-for-byte staged equality (unchanged).
- Region guard boundaries (above).
- Empty and oversized images, offset jumps, chunk overruns (unchanged).
- A1 invalidation power loss AFTER the erase: the marker page reads
  all-ones (factory state), A1 fails, a fresh cycle recovers.
- A1 invalidation write failure BEFORE the erase: the old marker survives
  untouched, A1 fails, a fresh cycle recovers.
- A3 commit power loss AFTER the erase: no length is committed, the
  commit fails, a fresh cycle recovers.
- Invalid metadata: an A3 claiming a length that was never staged is
  rejected and the marker stays invalidated.

Failures observed during development (recorded, then fixed): the first
implementation of the invalid-metadata check asserted the wrong expected
marker (all-ones instead of the A1-invalidated zero) and failed; corrected
before commit. The first CMake wiring still pointed at the V203 SDK
(after the platform directory rename) and failed the dependency check;
corrected.

## Harnesses re-run against the X035 code

All host harnesses now compile the `ch32x035` platform: the QMK-core
behavior harness (typing, modifiers, LT tap/hold, sticky SYM, consumer
encoding, runtime-only persistence across re-inits, chatter starvation,
6KRO, 200k-packet fuzz), the protocol pump (chord/pending lifecycle, idle
resend, suspend/resume, GET_REPORT, plain tap), session replay (15
scenarios), the auth I2C waveform (4) with the X035 timing assertion
(24 iterations at 48 MHz = stock's 1.5 us half-bit), consumer, backlight,
keymap, update protocol, and the restore harness above. The obsolete V203
flash-model shim was deleted rather than adapted: no mock carries V203-era
assumptions anymore. The iOS checks (protocol, layout, flash-path with the
stock-only gate, stream adapter) run in the preflight unchanged.

## Authorization distinction (preserved)

The firmware implements the A1/A2/A3/A0 update dialect for both
directions (custom update and restore-to-stock), because that is the
mechanism the stock installer uses. The iOS application's authorization
policy is independent and stricter: it stages only the exact stock V122
artifact onto an identified CK-5200 stock unit, and custom flashing is
disabled until a separately reviewed image is authorized. A firmware-side
update mechanism is not an app-side permission.

## Unresolved hardware-dependent behavior (evidence-based list)

- USB enumeration with the stock-identical descriptors on real X035
  silicon (never exercised by any host).
- The MFi auth chip: address, timing tolerance at the scaled half-bit, and
  certificate streaming against a real chip.
- iPhone acceptance: EA session open, alt-setting-1 EP3 pair, HID
  descriptor, charging behavior, attach/detach, sleep/wake.
- The Clicks installer's acceptance of a staged non-vendor image, and its
  behavior on an interrupted install (the boot region remains unread).
- Physical matrix wiring (inferred pin map, hardware-unverified),
  backlight LED wiring, real-key debounce feel.
- Independent recovery after an unbootable application: UNVERIFIED and
  blocked pending the PC17 investigation (`docs/recovery-research.md`).

## GO/NO-GO

**NO-GO for flashing the CK-5200**, unchanged. This port produces a
correct-family image with offline evidence; it does not produce recovery
evidence. The first live execution of the custom firmware stays separately
authorized and blocked until an independent recovery path is demonstrated
on this specific keyboard. The firmware is recoverable-by-design only in
the same sense stock is (the update dialect survives application faults
that leave USB alive); recovery from an application that never boots
remains unverified.
