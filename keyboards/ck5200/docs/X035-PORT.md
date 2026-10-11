# CH32X035 port report, 2026-10-12 (rev 2)

Status of the re-target ordered in the 2026-10-12 review: the firmware is
built for the CH32X035 family the stock firmware proves, with offline
evidence for every claim. Nothing in this work touched the physical
CK-5200: no flash, no erase, no ISP entry, no electrical manipulation.

**Rev 2 (same day)**: the adversarial verification of the first delivery
found four proven defects and several overstatements in this report. All
are corrected in the findings section below; every correction carries its
own regression test. The previous revision's claims are superseded
wherever this text differs. The verification's central lesson is now
part of the record: a passing local suite coexisted with a source-level
contradiction, a fault model that destroyed its own damage, and a red CI
run. Acceptance criteria from here on: green clean CI, source-level
verification of every hardware claim, and fault tests that preserve the
exact failure state they purport to recover from.

## Remediation of the verification findings

| # | Finding | Severity | Correction | Regression |
| --- | --- | --- | --- | --- |
| 1 | `protocol_setup()` still called `tud_init(1)` while the report claimed port 0 | P0 | `protocol.c` now calls `tud_init(0)`; the source contains exactly one `tud_init` call. Note: LTO inlines the call, so the ELF has no `tud_init` symbol to inspect; the executed test is the evidence | New scenario P6 in the protocol harness runs the real `protocol_setup()` and asserts `tud_init(0)` fires exactly once and `tud_init(1)` never; the harness records the port argument in its stub |
| 2 | CI run 38147753811 failed: duplicate definitions of `staging_host_*` between the harness and the driver broke linking under Linux `-fno-common` | P0 | Ownership fixed: the driver defines the counters, the harness only declares them via `ck5200_staging_host.h` | A green clean GitHub Actions run on Linux (the URL is recorded in the CI section) |
| 3 | The fault model called `full_cycle()` (which blanked the whole simulated flash) for "recovery" tests, destroying the damage | P0 | The harness separates `blank_flash()` (factory state only) from `power_cycle()` (MCU reset: volatile state cleared, flash PRESERVED) and `run_cycle()`; all recovery tests now run over retained damage | Every fault test asserts the damage state before recovery (e.g. `marker_read() == custom_len` retained, then recovery); chained stock/custom/stock cycles run without re-blanking |
| 4 | No fault during partial page programming; "safe commit" was overclaimed | P0 | Two new fault phases model the worst case the SDK cannot exclude: `AFTER_PROGRAM_MARKER` (marker word programmed, payload erased) and `HALF_PROGRAMMED` (half the page programmed). The harness proves a plausible marker can coexist with erased payload, that our A1 invalidation clears it, and the residual risk is stated explicitly below | The two worst-case scenarios assert the plausible marker value and the A1 recovery; the analysis is in the fault matrix |
| 5 | CI ran tests before the build and never provisioned the stock fixture | P0 | Workflow order is now setup -> hash-verified stock download -> build -> tests; `revert-stock.sh --download-only` fetches and sha-checks the fixture | The green CI run exercises exactly this order |
| 6a | `#if defined(USBFS_IRQn)` never fired (enum constant, not a macro) | P1 | Unconditional `_Static_assert`s for `USBFS_IRQn`, `USBFS_BASE`, and `HSI_VALUE` | Negative test in `test.sh`: `family_gate.c` compiled against a deliberately wrong-family stub must FAIL to compile (it does; the suite would exit nonzero otherwise) |
| 6b | `ck5200_staging_page_in_range(0xFFFFFF00)` wrapped the add and passed | P1 | Subtraction-based bound: `address <= STAGING_WRITE_LIMIT - FLASH_PAGE_SIZE` (small constants, cannot wrap) | Boundary tests including `0xFFFFFF00`, `0xFFFFFFFF`, `0xFFFF0000`, and 4096 randomized high probes |
| 6c | "Three independent layers guarantee all writes stay in staging" overstated | P1 | The report now states the accurate property: the staging writer has local bounds checks; the linker constrains only where the image is LOADED, not every address runtime code could write | The corrected claim in this document |
| 7 | D+ and D- swapped in the report's USB-pins row | P1 | Corrected: PC16 = D- (UDM), PC17 = D+ (UDP); PC17 is also the BOOT-detection pin | The corrected table cites the pin-function reference; no code change was needed (the BSP-copied GPIO setup was already correct) |
| 8a | `gpio_set_pin_output_open_drain` silently became push-pull | P1 | Caller audit recorded in source: no in-repo caller relies on open-drain semantics (the auth driver uses its own drive/release register sequence); the wrapper is documented as NOT open-drain, and future wired-AND callers must be rejected on this family | Source-level audit note; the auth harness covers the actual release sequence |
| 8b | SDA release described as pull-up input; the code uses floating input | P1 | Documented in source: floating input exactly as stock does at 0x5090; safe only with an external SDA pull-up, which the stock sequence evidences but which remains hardware-unverified | The host waveform harness covers the bit structure; the electrical fact is listed in the unresolved section |

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
size     24124 / 0x5e3c (max 0x6a00, region 0x2000..0x8a00, 88.90%)
sha256   7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623
entry    0x2000 (reset JAL -> 0x76fa)
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
| USB port | `tud_init(0)` / `tud_int_handler(0)`; IRQ 45 = `USBFS_IRQHandler` at vector slot 45. Corrected in rev 2 (finding 1): the first delivery claimed port 0 in this table while the source still called `tud_init(1)`; the source is fixed and the executed P6 test now backs this row | SDK `ch32x035.h:72` `USBFS_IRQn = 45`; BSP `family.c` uses port 0; protocol-harness scenario P6 |
| USB pins | PC16 (D-, UDM) / PC17 (D+, UDP) configured in `platform_setup`, PHY enabled via `AFIO->CTLR` (`USB_PHY_V33 \| UDP_PUE \| USB_IOEN`), USBPD block powered down | BSP `family.c:81-94` (PC16 floating, PC17 pull-up: D- and D+ respectively, per the CH32X035 pin-function reference); stock 0x41d0/0x3b04. Corrected 2026-10-12: the first version of this table swapped D+ and D- (finding 7). Note PC17 = D+ is also the BOOT-detection pin under investigation for recovery (`recovery-research.md`). |
| Clock | 48 MHz HSI, no PLL; `SystemCoreClock` fixed by the BSP system file; family gate asserts `HSI_VALUE == 48000000` | SDK `ch32x035.h:22`; stock 0x3bba/0x3bfc |
| Flash | 256-byte pages, fast-mode programming (`FLASH_BufReset`/`FLASH_BufLoad`/`FLASH_ProgramPage_Fast`); SDK bounds code flash at `FLASH_BASE + 0xF800` | SDK `ch32x035_flash.c:59-60,607-760`; reference manual ch. 20 |
| GPIO | `GPIO_Speed_50MHz` (the only speed enum); no open-drain output mode exists. The `gpio_set_pin_output_open_drain` wrapper is documented as NOT open-drain (it configures push-pull); its caller audit found no in-repo user of open-drain semantics -- the iAP2 auth driver releases SDA through its own drive/release register sequence (floating input, stock 0x5090), never through this wrapper | SDK `ch32x035_gpio.h:25,26-35`; caller audit in `gpio.c`; finding 8 |
| Persistence | ALL flash-backed persistence disabled: keymap lives in QMK's transient (RAM) EEPROM; edits are runtime-only and lost on power loss | review requirement 5; no writable region exists (see memory map) |
| Linker | Unchanged layout: `0x2000` base, `0x6A00` limit, 20 KB RAM, 2 KB stack; the application-limit assert still runs | `firmware/platform/ch32x035/linker.ld` |

## Inspection results (from the built ELF)

Verified with `llvm-objdump` on `build/ck5200_qmk.elf`:

- USB peripheral: 19 references to `0x40023xxx`, zero to `0x50000xxx`. The
  V203 image's fatal address is gone.
- Clock init: `SystemInit` sets HSION only; no PLLON (bit 24) write exists
  anywhere in the image.
- Vector table (absolute-entry mode, base `0x2004`): slot 45 = `0x6b80`
  = `USBFS_IRQHandler` (a real handler), slot 46 = `0x6e5a`
  = `USBFSWakeUp_IRQHandler`; the V203's slot 59 is `0x00000000`, exactly
  as stock's is.
- Staging: `marker_write` computes its base as `0x08008A00` (`lui 0x8009;
  addi -0x600`).
- Root port: LTO inlines `tud_init` into `protocol_setup`, so the ELF has
  no `tud_init` symbol to inspect; the executed protocol-harness scenario
  P6 is the evidence that the argument is 0 (finding 1).
- `validate_image.py` (full ELF/BIN validation): entry `0x2000`, `.vector`
  matches `_vector_base`, all sections inside the application region, RAM
  bounds hold. The boot region (`0x0..0x2000`) contains no loadable data.

## Flash-region protection (accurate statement)

The verifier's finding 6c is accepted: the previous "three independent
layers" wording overstated the property. The accurate claim: THE STAGING
WRITER HAS LOCAL BOUNDS CHECKS. The linker and validator constrain where
the image is LOADED, not every address runtime code could write; nothing
in this repo proves that no future code path writes outside staging.

1. Compile time: `firmware/platform/ch32x035/family_gate.c` (linked into the
   firmware) unconditionally asserts `USBFS_BASE == 0x40023400`,
   `USBFS_IRQn == 45`, `HSI_VALUE == 48000000`, rejects
   `SYSCLK_FREQ_144MHz_HSI` outright, and asserts the staging limit stays
   below the documented code-flash end (`0x0800F7FF`; SDK `ValidAddrEnd =
   FLASH_BASE + 0xF800`). The negative test in `test.sh` proves a
   wrong-family constant set fails compilation.
2. Link time: the linker's application-limit assert; the validator's
   region checks (no loadable byte outside `0x2000..0x8A00` plus RAM).
   These constrain the IMAGE, not runtime writes.
3. Runtime: `ck5200_staging_page_in_range()` admits only aligned 256-byte
   pages inside `[0x08008A00, 0x0800F500)`, using a subtraction-based
   bound that cannot wrap (finding 6b: the previous add-based check passed
   `0xFFFFFF00`). The restore harness unit-tests every boundary including
   the overflow probes: installer `0x08000000` and application
   `0x08002000` rejected; staging first/last page accepted; `0x0800F500`,
   serial `0x0800F600`, config `0x0800F700`, reserved `0x0800F800`,
   `0xFFFFFF00`, `0xFFFFFFFF`, and 4096 high-address probes all rejected;
   unaligned addresses rejected.

## X035 flash staging with fault injection

The staging writer now models the real X035 constraints: every write is a
page erase followed by a buffer-loaded page program, and both length-marker
operations (A1 invalidation, A3 commit) are page read-modify-write
transactions, because the marker shares its page with the first 252 staged
bytes and the SDK exposes no word programming.

The restore harness (`tests/ck5200_restore_test.c`, host model with
erase-then-AND program semantics) was REBUILT after finding 3 and 4: flash
state, MCU reset, and transaction execution are now separated
(`blank_flash` / `power_cycle` / `run_cycle`), and every recovery test runs
over the retained damaged flash, never over a re-blanked device.

### Fault matrix (injected condition -> preserved state -> behavior -> limitation)

| # | Injected condition | Preserved flash state after the fault | Observed behavior (all asserted) | Limitation |
| --- | --- | --- | --- | --- |
| 1 | A1-invalidate page write fails BEFORE the erase | Previous valid marker intact; page untouched | A1 rejected; power-cycled recovery overwrites the old marker | The surviving valid marker is the pre-existing stock-design window until the retry's A1 runs |
| 2 | A1-invalidate power loss AFTER the erase | Marker page all-ones (factory state); payload bytes of that page erased | A1 rejected; no install can be pending; recovery cycle completes over the damaged page | None modeled |
| 3 | A3-commit page write fails BEFORE the erase | Whole first staging page untouched from the A2 flush (marker still all-ones) | A3 rejected; no install pending; recovery completes | None modeled |
| 4 | A3-commit power loss AFTER the erase | Marker page all-ones; 252 payload bytes lost | A3 rejected; no install pending; recovery completes | None modeled |
| 5 | A3-commit partial program: marker word programmed, payload erased (worst case) | PLAUSIBLE marker (0x140) over erased payload | A3 reports FAILURE while a plausible marker exists; our A1 invalidation clears it on the next boot | **UNRESOLVED SAFETY RISK**: whether the controller can physically produce this state is unknown (the SDK's busy-wait page program has no status checking); if it can, the boot-region INSTALLER runs before our firmware and may treat the plausible marker as install-pending and copy erased payload. Cannot be excluded without decoding the installer. The stock updater shares the same window (its commit is the same page-RMW shape at 0x31b2). The commit is NOT atomic and was never atomic in the stock design |
| 6 | A3-commit partial program: half the page programmed | Marker (word 0, in the programmed half) plausible over a half-erased payload | Same as 5: failure reported, plausible marker, A1 clears it | Same as 5 |
| 7 | A3 claims a length that was never staged | Marker at its A1-invalidated zero | Rejected; marker unchanged; recovery completes | None modeled |
| 8 | No fault (chained cycles) | stock -> custom -> stock -> custom -> stock, no re-blanking between | Every cycle commits the right length and byte-exact payload | None modeled |

Every row executes the real update handler and the real staging writer
against the X035-constrained model. Rows 5 and 6 are the honest worst
cases the SDK's API cannot exclude: they are modeled, observed, cleared
by our A1, and classified as residual risk rather than proven safe.

### Region guard, including overflow

The runtime guard now uses a subtraction-based bound (finding 6b). The
harness rejects: installer and application pages, `0x0800F500` onward,
serial `0x0800F600`, config `0x0800F700`, reserved `0x0800F800`, the
wrap probes `0xFFFFFF00`, `0xFFFFFFFF`, `0xFFFF0000`, and 4096
high-address probes; it accepts exactly the staging pages
`0x08008A00` and `0x0800F400`.

### Failures observed during development (recorded, then fixed)

- The first invalid-metadata check asserted the wrong expected marker
  (all-ones instead of the A1-invalidated zero) and failed; corrected.
- The first CMake wiring still pointed at the V203 SDK after the platform
  rename and failed the dependency check; corrected.
- The first fault tests staged only 16 bytes, so the armed fault fired on
  the final-payload page write instead of the marker RMW; restaged at 320
  bytes so the fault provably hits the commit transaction.
- The first rebuilt harness assumed a committed marker survived the
  bounds-rejection block, but that block leaves an open transaction with
  the marker invalidated; a completing cycle was added first.

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
  certificate streaming against a real chip. The external SDA pull-up the
  floating-input release relies on is inferred from stock's identical
  sequence, not measured (finding 8b).
- iPhone acceptance: EA session open, alt-setting-1 EP3 pair, HID
  descriptor, charging behavior, attach/detach, sleep/wake.
- The Clicks installer's acceptance of a staged non-vendor image, and its
  behavior on an interrupted install (the boot region remains unread).
- Fault-matrix rows 5 and 6: whether the physical flash controller can
  produce a partially programmed page during the busy-wait page-program
  command. The host model shows the consequence if it can; only the
  installer decode or hardware evidence can close it.
- Physical matrix wiring (inferred pin map, hardware-unverified),
  backlight LED wiring, real-key debounce feel.
- Independent recovery after an unbootable application: UNVERIFIED and
  blocked pending the PC17 investigation (`docs/recovery-research.md`).

## CI evidence

The first delivery's clean-run claim was false: run 38147753811 failed on
the duplicate-symbol link error (finding 2), and its order ran the
image-dependent tests before the build (finding 5). The workflow is
corrected to: setup -> hash-verified stock fixture download -> build ->
tests -> ELF inspection -> artifact upload.

**Green clean run for this revision**:
https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/38165492867
(run 38165492867, head `13dd2c7`, job `build`: success, 54 s, Linux).

**Source-of-truth checksum comparison** between the CI-built BIN and the
iOS-bundled BIN:

```text
CI artifact  ck5200_qmk.bin  sha256 7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623  24124 bytes
Local build  ck5200_qmk.bin  sha256 7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623  24124 bytes
iOS bundle   ck5200_qmk.bin  sha256 7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623  24124 bytes
```

Byte-identical across all three. The CI disassembly confirms the family
footprint: 19 `0x40023xxx` references, zero `0x50000xxx`.

## GO/NO-GO

**NO-GO for flashing the CK-5200**, unchanged. This port produces a
correct-family image with offline evidence; it does not produce recovery
evidence. The first live execution of the custom firmware stays separately
authorized and blocked until an independent recovery path is demonstrated
on this specific keyboard. The firmware is recoverable-by-design only in
the same sense stock is (the update dialect survives application faults
that leave USB alive); recovery from an application that never boots
remains unverified.
