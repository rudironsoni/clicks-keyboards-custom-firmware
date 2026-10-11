# Flash-safety review, reviewer 2 of 3 (veto holder), 2026-10-11

Verbatim report of the independent firmware reverse-engineering and
hardware-recovery reviewer, run against commit `3586b76` in an isolated agent
context. The reviewer had veto authority over flashing. Provenance: adversarial
review session `ses_ed88066a4ffeJzebziCRxihFAS`, read-only, all scratch output
outside the repository. The consolidated verdict and the fix status of every
finding are in `2026-10-11-flash-safety-review.md`.

---

## A. Flash-safety verdict

# NO-GO — confirmed unsafe condition

A first flash of `build/ck5200_qmk.bin` (sha256 `7aeb06d2478853fdd46216cea3ea81a70145ab742cf6ae6988782e6ccff3b98a`, 23,392 bytes) onto the only CK-5200 must not proceed.

**The single decisive finding, established from primary evidence:** the stock firmware image and the proposed custom image are built for two different WCH MCU families. The stock image targets a **CH32X035-class MCU** (USB peripheral at `0x40023400`, USB interrupt at IRQ 45, 48 MHz HSI with no PLL). The custom image targets a **CH32V203-class MCU** (USB peripheral at `0x50000000`, USB interrupt at IRQ 59, 8 MHz HSI driving a 144 MHz PLL). If installed, the custom firmware hangs in its own `SystemInit` waiting for a PLL lock that cannot occur, before a single byte of application code runs. USB will never enumerate. The only documented recovery channel (the A1/A2/A3/A0 dialect over USB) then requires exactly the firmware that just failed to boot, and the only remaining path (physical flash programming) is unverified on this board.

This verdict is independently sufficient. A second, also-sufficient reason stands behind it: recovery is not demonstrated (unknown installer, zero full backup, no tested independent restore path), which is precisely the condition `docs/RECOVERY.md:3` already states: "Recovery is not verified. Do not flash custom firmware yet."

## 1. What the hardware actually is (evidence)

### 1.1 The first-class open question: what the stock images cover

**VERIFIED by my own byte-level inspection:**

| Artifact | Size | Load base | Covers 0x08000000..0x08002000? |
| --- | --- | --- | --- |
| `.stock/iKeyboard_CK-5200_V122_120.bin` (sha256 `8ee86935...`, matches preflight pin) | 18,648 bytes (0x48D8) | 0x00002000 | **No** |
| `.stock/iKeyboard_CK-5200_V121_120.bin` (sha256 `1c9ae5b2...`, matches pin) | 18,572 bytes (0x488C) | 0x00002000 | **No** |
| `build/ck5200_qmk.bin` | 23,392 bytes (0x5B60) | 0x00002000 | No (custom app) |
| `ios/ClicksInspector/Resources/*` (2 files) | 18,648 / 23,392, hashes match the above | 0x00002000 | No |
| `.stock/audit/*` | ELFs + disassembly derived from the same two .bin files, plus `NOTES.md` | 0x2000 | No |
| `clicks-app-prod-release.apk` (32.5 MB, 1,183 entries) | contains no firmware `.bin` (only `DebugProbesKt.bin`, a Kotlin class file) | n/a | No |

**The docs' claim that the boot region was never dumped is TRUE.** No artifact on this machine contains the bytes at `0x08000000..0x08002000`. `find` over the whole tree excluding `external/`, `.toolchains/`, `.venv/` found no other binary that could contain it; the APK was listed entry-by-entry.

**Load-base proof, independently reproduced** (not taken from docs):
- V122 word 0 (`6f20 d005`) = `j 0x485c`; word at file offset 4 = `0x00002000` (the mtvec/vector-table base). V121: `j 0x4112`, same `0x2000` at offset 4.
- The reset handler at `0x485c` sets `gp = 0x20000840` (`auipc gp,0x1fffc; addi gp,gp,-0x1c` at `0x485c`/`0x4860`), `sp = 0x20005000` (`0x4864`/`0x4868`), and copies `.data` to `0x20000000` (`0x486c`/`0x4874`/`0x4878` resolve to exactly RAM 0x20000000 only at base 0x2000; at base 0 they would resolve to 0x1FFFE000, which is not RAM on this part). This reproduces `HARDWARE.md:9-12` exactly.
- 20 KB RAM (sp top 0x20005000) and 64 KB flash address space are consistent with both CH32V203C8-class and CH32X035-class parts; RAM alone does not discriminate. The USB and clock evidence below does.

### 1.2 The MCU family: stock is CH32X035-class, not CH32V203-class

`HARDWARE.md:22` says "[INFERENCE] The code and address map are consistent with a CH32V203-class MCU" and `firmware/platform/ch32v20x/platform.c:52` says "The stock CK-5200 uses the CH32V20x USBFS block at 0x50000000." Both are refuted by the stock image itself. Five independent discriminators, all VERIFIED from my own disassembly of V122 (base 0x2000, via `mkelf.py` + `llvm-objdump --mattr=+m,+a,+c,+xwchc`):

1. **USB register base.** Stock's USB driver operates on `0x40023400`: `lui a5, 0x40023` then byte/word accesses at +0x400 (`BASE_CTRL`, 0x3f3e), +0x401 (`UDEV_CTRL`, 0x3f6c), +0x403 (`DEV_ADDR`, 0x3dbe), +0x406 (`INT_FG`, 0x40f8), +0x408 (`RX_LEN`, 0x3eb6), +0x40C (`UEP4_1_MOD`, 0x3d62), +0x40D (`UEP2_3_MOD`). The stock image never once loads `0x50000000`. CH32X035 SDK: `USBFS_BASE = AHBPERIPH_BASE + 0x3400 = 0x40023400`. CH32V203 (openwch master and vendored header, line 870): `USBFS_BASE = 0x50000000`.
2. **Endpoint register layout.** Stock indexes per-endpoint control words at +0x22 with 4-byte stride (`addi a5, a5, 0x422; slli a0, a0, 0x2` at 0x3dc6/0x3de2/0x3e36) and touches +0x10..+0x1C (UEP0-3 DMA), +0x20..+0x32 (UEP0-4 TX_LEN at word, TX_CTRL/RX_CTRL bytes at +2/+3). This is the CH32X035 `USBFSD_TypeDef` layout exactly. The CH32V203 layout (TX_LEN block at +0x30..+0x4F, DMA at +0x10..+0x2C for 8 endpoints) leaves stock's +0x22/+0x26/+0x28/+0x2A/+0x2C/+0x2E accesses meaningless.
3. **Interrupt number.** Stock's vector table (entry i at file offset 4+4i): **IRQ 45 = 0x405a, a real handler** that acknowledges `INT_FG` at `0x40023406`; IRQs 38-44 and 46-54 all point to the default handler chain at 0x48xx; **IRQ 59/60 = 0x00000000** (not even populated). CH32X035: `USBFS_IRQn = 45`. CH32V203: `USBFS_IRQn = 59`.
4. **Clock tree.** Stock `SystemInit` at `0x3bba` writes only `RCC->CTLR |= 1` (HSION); the image never writes PLLON (bit 24) anywhere; `SystemCoreClock` is computed from the constant `0x2DC6C00` = 48,000,000 at `0x3bfc`, and the `.data` initializer at file offset 0x48C0 is `00 6c dc 02` = 48 MHz. HSI = 48 MHz, no PLL, USB clocked directly — the CH32X035 model (`HSI_VALUE = 48000000`, no main PLL; USB runs from HSI). CH32V203: HSI = 8 MHz, and per the CH32V203 datasheet the USB 48 MHz clock is generated by the main PLL (USB requires CPU at 48/96/144 MHz).
5. **RCC enable bits.** Stock disables AHB bit 17 (0x20000) at boot (`0x4c46(0x20000, 0)` at 0x3b04/0x3b08) and enables 0x1000 in USB init (`lui a0, 0x1` at 0x41d0). On CH32X035: `RCC_AHBPeriph_USBFS = 0x1000`, `RCC_AHBPeriph_USBPD = 0x20000` — the X035 has a USB-PD PHY, which stock powers down. A USB-PD-capable MCU in a battery case that passes charging through to the iPhone is also the plausible product-level explanation.

Supporting match (same on both families, so not discriminatory but consistent): GPIOA at `0x40010800` (stock iAP2 driver uses `0x40010804` CFGHR / `0x40010808` INDR / `0x40010810` BSRR), TIM1 at `0x40012C00` (backlight), SysTick at `0xE000F000`, PFIC reset via `*(0xE000E048) = 0xBEEF0080`, option bytes `0x1FFFF800`.

**Conclusion: VERIFIED that the stock application is written for a CH32X035-family part. INFERENCE (very high confidence, from five independent register-level matches against the official openwch/ch32x035 SDK headers, fetched during this review) that the physical chip is a CH32X035-class part.** The chip marking has never been read (`HARDWARE.md:22`, `RECOVERY.md:57`), and I could not read it under the operating rules. I checked the alternative explanations (CH32V103: older USB IP, 8 MHz HSI, different IRQ; CH32V208: 64 KB RAM, IRQ 59; a hypothetical V203 variant with a moved USB block: contradicted by WCH's current master SDK and by TinyUSB/QMK's working V203 support at 0x50000000). None survives the IRQ-45/0x40023400/48 MHz-HSI evidence triple.

### 1.3 What the custom image is built for

VERIFIED from the linked binary itself (`llvm-readelf`/`llvm-objdump` of `build/ck5200_qmk.elf`):
- Built with `startup_ch32v20x_D6.S`, `CH32V20x_D6`, `SYSCLK_FREQ_144MHz_HSI=144000000` (`CMakeLists.txt:15,111-112`), TinyUSB `hw/bsp/ch32v20x/system_ch32v20x.c` (`SetSysClockTo144_HSI`: HSI/2 x 18 = 144 MHz PLL, USB = PLL/3).
- `SystemInit` at 0x66e2: enables PLLON (`RCC->CTLR |= 0x01000000`, 0x6758-0x6760) then spins `while(!(CTLR & (1<<25)))` (0x6762-0x6768, `bgez` self-loop). A CH32X035 has no main PLL; bit 25 will not set. **The firmware hangs here, before `.data`/`main`/USB/TinyUSB execute.** (`.data` copy and `mtvec` setup at `handle_reset` 0x7460 happen before `jal SystemInit` at 0x74da, so the hang is total.)
- USB driver: 16+ sites of `lui a4/a5, 0x50000` (first at 0x26d8); **zero** references to 0x40023400. TinyUSB's CH32V20X branch (`src/portable/wch/ch32_usbfs_reg.h:111-112`) includes the ch32v20x vendor header, i.e. `USBFS_BASE = 0x50000000`.
- Vector table is generated from the D6 startup with CH32V203 IRQ numbering (USBFS at slot 59, empty on X035 silicon; the X035's real USB IRQ, 45, points at a default/other handler in the custom table).
- `platform.c:53-54`: `RCC_USBCLKConfig(RCC_PLLCLK_Div3)` and `RCC_AHBPeriphClockCmd(RCC_AHBPeriph_OTG_FS, ...)` — V203 register semantics.

**Three independent fatal mismatches (clock, USB base, IRQ), any one of which alone prevents USB from ever enumerating.**

## 2. Independent stock-firmware analysis (A1/A2/A3/A0 and installer)

All addresses are linked addresses in V122 at base 0x2000; my own disassembly.

- **Dispatcher** at `0x3216`: `[len, cmd, ...]` framing; busy if `available < len` (`0x3226`); version `02 03` answered at `0x396e` with constant `08 02 03 00 01 20 01 22` (matches the live iPhone read recorded in `USB_AND_IPHONE.md:73`).
- **A1** at `0x37a8`: size bound `lui a4,0x7; addi a4,a4,-0x600` = 0x6A00 (`0x37aa`), reject above it; success path stores expected size at ctx+4, sets page-fill to 4, and writes `0xFFFFFFFF` into page bytes 0..3 (`0x37e2-0x37ea`) — the first staging page reserves the 4-byte length word, erased. Reply `08 02 A1 00 00 00 6A 00` (max echoed even on the error path).
- **A2** at `0x380a`: requires the request's offset to equal bytes-received-minus-4 (`0x3810-0x3812`), reply on mismatch is `08 02 A2 01 <expected BE32>`; chunk length = len-6 (`0x3816`), payload read from +6 (`0x38ba`); bytes buffered one at a time into a 256-byte page at ctx+0xC; page flushed to **`0x08008A00 + running offset`** (`lui s4, 0x8009; addi s4, s4, -0x600` at 0x3818/0x382a) through the page-writer at `0x3174` (flash unlock, erase page, program 64 words, lock); running offset advances 0x100 per page.
- **A3** at `0x38de`: pads the final page with 0xFF (loop at `0x3948`), writes it (page RMW writer `0x31b2`/`0x3174`), then programs the 4-byte size at `0x08008A00` (`lui a0, 0x8009; addi a0, a0, -0x600; li a2, 4; addi a1, s0, 4; jal 0x31b2` at `0x38fe-0x390c`; the stock word-programmer at `0x31b2` is a full 256-byte page read-modify-write). Reply `08 02 A3 00 <size BE32>`.
- **A0** at `0x3954` (dispatched from `0x3286`): sets a state bit, replies `04 02 A0 00`; the main loop at `0x3a20-0x3a32` then writes `0xBEEF0080` to `0xE000E048` (PFIC_CFGR) and spins — exactly the WCH `NVIC_SystemReset()` (verified in both the vendored V203 SDK and the X035 SDK, `core_riscv.h:338-340/404-406`). Full chip reset; control returns to whatever is at `0x08000000`.
- **Staging reach**: max image 0x6A00 → last staged byte at 0x0800F404, page-rounded writes end at 0x0800F500. Stock config lives at `0x0800F700` (12 bytes, saved via the same RMW when dirty, `0x3a0e-0x3a18`), and the **device serial is the word at `0x0800F600`** (getter `0x2276`: `lui a5,0x800f; lw a3,0x600(a5)`, then decimal serialization into RAM). Neither region is touched by staging. Stock never writes below 0x2000 (all `lui 0x8009/0x800f` flash references enumerated).
- **V121 vs V122**: 17,379 differing bytes across file offsets 0x2..0x4889 — a full rebuild, not a patch; the only semantic change visible is the version string `1.2.1` → `1.2.2` at 0x4775/0x47c1. Both implement the identical staging dialect at the same physical addresses (V121 `lui 0x8009` at 0x37ce/0x38a2/0x38b4). No undocumented compatibility constraint between V121 and V122 is visible.
- **The installer (0x08000000..0x08002000) is absent from every artifact.** Therefore the following remain UNKNOWN and are properties of never-read code: whether it validates a signature or checksum of the staged image; whether it rejects unknown images and what it does on rejection (fall back to the old app? hang? erase first?); whether and when it clears the commit marker; what it does if the application region is corrupt; whether it ever exposes USB itself. `HANDOFF.md:179-183` admits the signature question; `RECOVERY.md:22` names both unknowns. **The claim in `FLASHING.md:141` that "the boot region's installer can retry from a fresh A1/A2/A3 cycle if USB still enumerates" is an assumption about unread code.** The strongest true statement is: staging and reboot semantics are VERIFIED (stock image); install-and-fallback semantics are UNVERIFIED.

## 3. Stock versus custom compatibility matrix

| Subsystem | Stock (evidence) | Custom (evidence) | Consequence | Status |
| --- | --- | --- | --- | --- |
| MCU family | CH32X035-class: USB @ 0x40023400, IRQ 45, HSI 48 MHz no PLL (V122 dis 0x3bba, 0x3bfc, 0x3d5e-0x4264, vector slot 45) | CH32V203-class: USB @ 0x50000000, IRQ 59 slot, PLL 144 MHz wait (custom dis 0x66e2-0x6768, 0x26d8+, D6 startup) | **Custom hangs at boot; USB dead. Keyboard unusable.** | **VERIFIED mismatch** |
| Reset vector / launch format | JAL at 0x2000, word 0x2000 at +4, mtvec base 0x2004 (V122 0x2000/0x2004, reset 0x485c) | Same format (custom header `6f50 0046`, word 0x2000, ELF entry 0x2000, `_start` 0x2000; `validate_image.py` enforces) | Compatible; installer can jump to it | VERIFIED compatible |
| Image bounds | A1 enforces ≤ 0x6A00 (0x37a8) | Linker FLASH 0x2000..0x8A00 + ASSERT; ELF/BIN validator; 23,392 B ends 0x7B60 | No overlap with staging 0x8A00 or boot region | VERIFIED |
| Staging writer | 0x08008A00/+4, 256-B pages, first page reserves length, A3 commits size, A0 resets (0x37a8-0x3946, 0x3a24) | Byte-for-byte same layout and commit semantics (`ck5200_staging.c:22-30,76-79,125-163`; harness-verified offline) | Restore dialect matches stock | VERIFIED (protocol), UNVERIFIED (physical flash behavior of X035 fast-program via V203 SDK calls — same WCH flash IP, but untested) |
| USB enumeration | Stock descriptors, EP 81/01, 82/02, 83/03 alt 1 (live reads 2026-10-08, `USB_AND_IPHONE.md:7-11`) | Same topology claimed; but driven at the wrong peripheral base and wrong IRQ → never enumerates | Fatal | VERIFIED fatal |
| A0 reply | `04 02 A0 00` sent before reset (0x395c-0x396c) | No reply; reset immediate from callback (`ck5200_update_protocol.c:38-41`) | Cosmetic protocol deviation; hosts ignore the A0 reply | Minor, intentional-looking |
| Mac restore command | n/a | `FLASHING.md:239,296` documents `ck5200_usb.py restore-stock`; **no such subcommand exists** (only inspect/packets/flash; restore is `flash <stock.bin>`, and `revert-stock.sh:60` wraps exactly that) | Doc defect; recovery instructions reference a nonexistent command | CONFIRMED defect (doc) |
| Keymap / matrix / backlight / iAP2 decode | GPIOA 0x40010800, TIM1 0x40012C00, PA13/14 I2C (dis) | Same addresses (family-portable) | App-level decodes remain plausible despite the family error | VERIFIED portable addresses; runtime UNVERIFIED |
| Keymap persistence | Stock: config pages 0x0800F600/0x700 in code flash; serial 0x0800F600 | Custom: wear-leveled store at 0x0800F800..0x0800FC00 (`wear_leveling_ch32v20x.c:16`, `config.h:28`) — on a CH32X035 this is the **2 KB DataFlash** region (62 KB code + 2 KB data), whose programming via the V203 SDK flash calls is UNVERIFIED | Custom feature risk, not a brick risk | UNVERIFIED |
| Identify serial | Runtime, from device-unique word at 0x0800F600 (getter 0x2276) | Hardcoded constant `2311000001` (`USB_AND_IPHONE.md:173` deviation 3) | Wrong serial reported to iPhone | Known deviation |
| Version/bcdDevice | 0x0122 | 0x9001 routing marker | Intentional; gates Mac tool | Intentional |
| Doc-set self-consistency | RECOVERY.md:3 blocks the flash | STATUS.md:3, FLASHING.md §5/§7, HANDOFF.md:206+ proceed | **Contradiction resolved by evidence: RECOVERY.md is right; see §D** | CONFIRMED contradiction |

## 4. Challenge the recovery design: the 14 scenarios

Resolution of the documented contradiction first, from primary evidence: `RECOVERY.md:3` ("Recovery is not verified. Do not flash custom firmware yet.") and `RECOVERY.md:56-66` (five unmet preconditions: chip marking, debug pads, protection state, boot-region backup, tested restore) are the evidence-supported position. `STATUS.md:3` ("ready for its first flash") and `FLASHING.md`'s guarantees are refuted by (a) the wrong-family image (§1.3), which converts FLASHING.md's "only unrecoverable case" (§7, lines 314-321: "No normal flash operation can cause [USB death]: our image never writes below 0x2000") into the *expected* outcome — the image never writes below 0x2000, but it also never runs, so USB dies anyway; and (b) `HANDOFF.md:14-17`'s "if USB still enumerates, the A1/A2/A3/A0 update dialect can always restore stock" is conditional on a USB-enumerating firmware, which this image will not produce.

Per scenario (Trigger / what still executes / recovery channel / tested?):

1. **Custom app does not boot.** Expected outcome of flashing this image (hang at 0x6762). Executable: only the boot-region installer (unknown behavior). Channel: none proven. Not tested. Depends on broken app: no. Physical programming required: yes, unverified.
2. **Startup infinite loop.** Literally what `SetSysClockTo144_HSI` does on a PLL-less part. Same as 1.
3. **USB enumeration fails.** Expected outcome (wrong base/IRQ). Installer fallback unknown; physical programming unverified; note PA13/PA14 (the WCH 2-wire debug pins per `RECOVERY.md:114`) are also the live I2C bus to the MFi auth chip — a real attach complication documented nowhere in the recovery guide.
4. **USB stack crashes after enumeration.** Would require the app running first. Restore then depends on the broken application — not independent.
5. **iAP2 session never opens.** The Mac EP2 dispatcher fallback exists in code (`protocol.c:249-253`); it is sound design, but it sits behind the same dead USB stack. Independent only at the source level, not at the hardware level.
6. **Vendor dispatcher unreachable.** Same dependency as 5.
7. **Staged image incomplete/corrupt.** If A3 never commits, the old marker (probably 0xFFFFFFFF after the last vendor install consumed it — INFERENCE on installer behavior) survives; installer action unknown. There is a real hazard window: a previously committed marker stays valid until the first staging page is rewritten (first 252 A2 bytes), so an interrupted re-flash can leave a valid marker over a hybrid staging area. `RECOVERY.md:68-72` states this honestly ("not a rollback").
8. **Power loss during staging/commit/install.** Same as 7 for staging; the install copy itself happens entirely inside the unread installer. No evidence of atomicity or retry.
9. **Reset between page operations.** The custom writer latches failure and requires a fresh A1 (harness-verified offline: `tests/ck5200_restore_test.c:173-198`). Physical flash behavior untested.
10. **Image needs flash/option changes.** Neither image writes option bytes; custom writes only staging (0x08008A00+) and its keymap store (0x0800F800+). VERIFIED no writes below 0x2000 in both images.
11. **Installer rejects the image.** Unknown outcome; spectrum from benign (boot old app) to brick (hang after erase). This is the untested gate the entire transaction passes through.
12. **Persistent config corrupt.** Stock serial (0x0800F600) and config (0x0800F700) are outside both images' write sets (VERIFIED); custom keymap store corruption is recoverable by `0x93` reset logic — if the app runs.
13. **Debug interface unavailable/read-protected.** Unknown; X035-family read-protection would make the `RECOVERY.md` physical route a stop condition, not a rescue. No evidence either way.
14. **Host disconnects mid-transaction.** Host tools abort cleanly and never send A3 after a failure (`ck5200_usb.py:176-194` drives offsets from device replies; Swift `UpdateChunker.applyAcceptedOffset` throws on desync). Leaves scenario-7 state.

**What can currently be recovered:** the stock *application* can be re-staged from the archived V122 bin — but only through a firmware that already boots and enumerates. **What cannot be demonstrated recoverable:** a first flash that does not boot or enumerate. **Backup coverage: zero bytes** (the honest table at `USB_AND_IPHONE.md:117-125` is accurate). **Which bootloader behaviors remain unknown:** all of them (acceptance, rejection, marker clear, corruption policy, USB capability). **Which physical interfaces must be verified:** the actual chip marking, the debug pads, the protection state. **What would establish an independent restore path:** a full physical readback (boot region + main flash + system/vendor/option regions) plus a demonstrated physical re-program and readback-verified restore, per `RECOVERY.md` §6-10 — all currently unmet.

## 5. Installation-transaction audit (host to flash)

Intended transaction (first flash, from the iPhone, since `ck5200_usb.py:262-266` refuses any device whose `bcdDevice != 0x9001` — i.e. the Mac tool cannot flash stock, only custom):

1. ClicksInspector (`CustomFirmwareController.swift:230-257`): A1 `[06 A1 <size BE32>]` → 32-byte A2 chunks, each reply's accepted offset drives the next chunk → A3 `[06 A3 <size BE32>]` → A0 `[02 A0]` fire-and-forget. Framing verified byte-identical to the stock dispatcher decode (§2). The app does **not** gate flashing on the custom identity, so first flash from stock over the EA session is reachable, as FLASHING.md §5 describes.
2. Stock device handlers perform the staging (VERIFIED, §2) including the 0x6A00 bound, sequential-offset enforcement, page writes, and the A3 commit word.
3. A0 → PFIC reset → **the unread installer does the actual install.** Everything before this sentence is verified; this sentence is the unverified 100% of the destructive step.

Weaknesses found in the write-capable paths (none change the verdict):
- `ck5200_usb.py` `--no-reboot` leaves a committed marker armed; any later reset installs ("The staged image may still be installed on a later reset", line 282) — correct warning, but the armed-marker window interacts with scenario 7.
- Single-in-flight reply design in the custom dispatcher (`protocol.c:224-225`) silently drops overlapping packets; both host tools pace on replies, so this is safe with these tools only.
- The custom A0 resets before replying (deviation, harmless).
- `FLASHING.md` documents a nonexistent `restore-stock` subcommand.
- STATUS.md:35-38 quotes a stale build (23,388 B, sha `dad34db6...`); the actual artifact everywhere else (including the iOS bundle and preflight) is 23,392 B, sha `7aeb06d2...`.

## 6. Bricking failure-mode ranking

| # | Failure mode | Severity | Likelihood if flashed | Detection before damage | Recovery | Smallest effective prevention |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | **Wrong-MCU-family clock init: infinite PLLRDY wait** (custom 0x6762) | **Permanent loss** | **Near-certain** (CH32X035 has no PLL) | None on device; detectable offline (this review) | Physical programming only, unverified | Re-target to the actual family; add a build-time assertion that the linked USB base/IRQ match the stock image footprint |
| 2 | **Wrong USB peripheral base (0x50000000 vs 0x40023400)** | Permanent loss | Near-certain if 1 somehow passed | None on device | Same | Same |
| 3 | **Wrong USB IRQ slot (59 vs 45)** | Permanent loss | Near-certain if 1-2 passed | None on device | Same | Same |
| 4 | Installer rejects/hangs on the staged image (unknown semantics) | Permanent loss | Unknown probability; consequences unknown | Impossible without the boot dump | Physical programming, unverified | Dump the boot region before any flash (`RECOVERY.md` §7-8) |
| 5 | Interrupted A2 leaves valid old marker over hybrid staging; reset installs a corrupt hybrid | Severe (app region corrupt) | Low-moderate window (first 252 staged bytes) | None | Depends on installer fallback (unknown) | Clear/invalidate the marker at A1 time (a one-word write before accepting), or verify installer semantics from the dump |
| 6 | Power loss during the install copy | Severe | Low | None | Depends on installer retry semantics (unknown) | Boot-region dump; only then model |
| 7 | Keymap store writes to X035 DataFlash via V203 SDK calls | Feature loss | Unknown | Testable offline only after re-target | Re-flash | Verify DataFlash programming on the correct SDK |
| 8 | Physical recovery attempt itself (wrong pads, protection, PA13/14 shared with auth-chip I2C) | Permanent loss | Moderate if attempted blindly | Measurable preconditions in `RECOVERY.md` §2-5 | n/a | Follow `RECOVERY.md` stop-conditions; read chip marking first |

## E. Blocking findings (confirmed) and unresolved risks

**Confirmed defects (blocking):**
1. **Wrong MCU family.** Custom image built for CH32V203; stock image proves a CH32X035-class part. Evidence: §1.2 (five discriminators), §1.3 (three fatal mismatches in the linked binary). Files: `keyboards/ck5200/CMakeLists.txt:15,111-112`; `keyboards/ck5200/firmware/platform/ch32v20x/platform.c:49-54`; vendored `ch32v20x` SDK + TinyUSB `ch32_usbfs_reg.h` / `system_ch32v20x.c`; stock disassembly addresses cited in §1.2.
2. **False hardware claim in the platform code.** `platform.c:52` states stock uses "the CH32V20x USBFS block at 0x50000000" — refuted by stock's own USB driver (0x40023400).
3. **Documentation contradiction resolved against flashing.** `STATUS.md:3`, `HANDOFF.md:25,206`, `FLASHING.md` proceed-to-flash guidance vs `RECOVERY.md:3,56-66`. Evidence supports RECOVERY.md; FLASHING.md §3's "What we can guarantee" (lines 152-157) and §7's "only unrecoverable case" (lines 314-321) are refuted by finding 1.
4. **Preflight blindness.** I ran `bash keyboards/ck5200/tools/preflight_check.sh` myself: **PREFLIGHT PASS: 10 checks, 0 failures** (all hashes, ELF/BIN validation, restore-cycle harness, Swift checks). None of the 10 checks compares the target's USB base, IRQ map, or clock model against the stock image. A 10/10 preflight coexists with a wrong-family image.

**Unresolved risks (each independently blocking for a GO, per my mandate):**
5. Installer semantics unknown (never dumped; no artifact contains the boot region — verified).
6. No full backup exists (zero bytes extracted from the device; `USB_AND_IPHONE.md:117-125` accurate).
7. No independent recovery demonstrated (phone and Mac restore both require the running custom app; physical route untested and its pins are shared with the auth-chip I2C bus).

**Minor findings (non-blocking):** nonexistent `restore-stock` subcommand in FLASHING.md; stale build numbers in STATUS.md; custom A0 sends no reply; hardcoded ident serial; keymap store lands on the X035 DataFlash region.

## F. Minimum path to an acceptable first-flash decision

In dependency order; steps 1-3 are offline, reversible, and cheap:

1. **Read the physical chip marking** (no writes, no risk). Confirm the exact part number/package. Expected: CH32X035-family. This converts my register-level identification into a fact.
2. **Re-target the firmware to the actual family** using the official ch32x035 SDK (HSI 48 MHz direct clocking; USB at 0x40023400 with the X035 register layout; USBFS IRQ 45; X035 startup/vector table). Re-run the full offline suite and the restore-cycle harness on the corrected image.
3. **Add family assertions to the build and preflight**: fail the build/validator if the linked USB base, USB IRQ slot, or clock-init writes deviate from the stock image's footprint. This review's method (compare `lui` base constants and vector slots against stock) is directly automatable.
4. **Establish physical debug evidence without writes** (`RECOVERY.md` §2-5): pads, voltage, protection state, with the PA13/PA14 I2C-sharing complication addressed.
5. **Capture a full readback** (main flash including boot region, system region, vendor/option bytes; `RECOVERY.md` §6-8) into two verified copies. This simultaneously (a) creates the only real backup, (b) makes the installer finally readable, and (c) settles marker semantics and image-validation behavior.
6. **Decode the installer** from the dump; model the commit marker and install copy; add the A1-time marker-invalidation fix if the dump confirms the interrupted-transfer hazard.
7. **Demonstrate an independent restore**: physical re-program of the dumped image on this board, verified by readback (`RECOVERY.md` §10), under separate consent for the write.
8. Only then reconsider the first flash as a **controlled experiment with residual risk** (auth-chip behavior, iPhone acceptance, real-key timing remain live tests), never as "ready."

Exact commands I ran and their results (abridged to the decision-relevant ones): `git log --oneline -5` / `git status --short` (HEAD 3586b76, clean); `shasum -a 256` on both stock bins, build bin, iOS resources (all match pins); `xxd` image headers (JAL + `0020 0000`); `unzip -l` APK (no firmware); `python3` V121/V122 diff (17,379 differing bytes, 0x2..0x4889); `mkelf.py` + `/opt/homebrew/opt/llvm/bin/llvm-objdump -d --triple=riscv32 --mattr=+m,+a,+c,+xwchc` for V121/V122 into scratch (address analyses in §1-2); `llvm-readelf -h -l` and `llvm-objdump` on `ck5200_qmk.elf` (§1.3); `bash keyboards/ck5200/scripts/test.sh` (all pass: 5+2 pytest, update-protocol `ok`, backlight `ok 1`, consumer `ok 7`, restore harness `full cycles ok (stock 18648, custom 23392)` / `bounds and jumps ok` / `failure injection ok`, iAP2 `ok 13`, auth `ok 4`); `bash keyboards/ck5200/tools/preflight_check.sh` (10/10 PASS); `curl` of openwch/ch32x035 and openwch/ch32v20x master headers (quoted in §1.2); datasheet facts via vendor/product documentation search (labeled above). Build-verification commands I did NOT run per the operating rules, recorded for the later execution phase: `bash keyboards/ck5200/scripts/build.sh`, `bash keyboards/ck5200/tools/verify_history.sh c1d9e7c`.

## Final answer

**Would you allow this image to be flashed onto the owner's only keyboard today? No.**

The firmware in `build/ck5200_qmk.bin` is built for a CH32V203-class MCU, and the stock firmware running on this keyboard proves, at the register level, that the keyboard is a CH32X035-class device: its USB peripheral is at 0x40023400 (not 0x50000000), its USB interrupt is IRQ 45 (not 59), and it runs on a 48 MHz internal oscillator with no PLL (not an 8 MHz HSI driving a 144 MHz PLL). The custom image's own `SystemInit` spins forever waiting for a PLL-ready bit that cannot exist on the target silicon, before USB or any application code executes. Flashing it very likely converts the owner's daily driver into a device that no host can see, and every documented restore path (the A1/A2/A3/A0 dialect from the phone or the Mac) requires a firmware that enumerates, which the flashed image would not be. The only remaining rescue is physical flash programming, which this project has not established on this board: the chip marking, the debug pads, the protection state, and the boot-region contents are all unknown, and no full backup of any byte on this device exists. The repository's own `docs/RECOVERY.md` states this correctly at its line 3, and the proceed-to-flash documents (`STATUS.md`, `FLASHING.md`, `HANDOFF.md`) are contradicted by the primary evidence. The offline engineering is otherwise disciplined — the staging protocol decode is byte-accurate, the harnesses run green under my own execution, and the preflight passes 10/10 — but all of it validates the transaction layer, not the machine it runs on. Re-target the firmware to the family the stock image actually names, prove physical readback and restore on this exact board first, and only then revisit the flash decision.
