# Recovery research: existing mechanisms only, evidence-cited

Current decision: [independent iPhone recovery remains UNMET](adr/0001-independent-iphone-recovery.md), 2026-10-15.
Supporting evidence: [2026-10-14 recovery audit](recovery-audit-2026-10-14.md).
The investigation priorities below are historical, not authorization for device operations. See [current readiness](flash-readiness.md).

Status: 2026-10-12. This report answers one question: can a broken custom
application on the CK-5200 be recovered WITHOUT that application executing,
under the constraints of no new hardware, no dev board, no PCB modification,
and no invented replacement bootloader. The governing requirement comes from
the flash-safety reviews: a working stock-to-stock update never satisfies it.

Sources are classified: VERIFIED (read from source or datasheet), REPORTED
(third-party users), INFERENCE (labeled), or UNKNOWN. Memory-map and pin
claims cite a chapter, an SDK/BSP source line, or a stock-image address,
nothing else.

## 1. The MCU-family facts (VERIFIED)

| Fact | Evidence |
| --- | --- |
| Main flash is 248 pages x 256 bytes, ending `0x0800F7FF` | CH32X035 reference manual ch. 20 (per the 2026-10-12 review); both wchisp and chprog device tables list CH32X035 `code_size: 63488` |
| `0x0800F800..0x0800FFFF` is RESERVED | Reference-manual memory map; the pinned TinyUSB BSP linker models `__flash_size : 62K` |
| Factory USB ISP exists on the family | wchisp README "Tested On" lists CH32X035; chprog DEVICES table lists seven CH32X035 variants |
| The ISP device is a plain USB vendor device | chprog source: VID `4348`/`1a86`, PID `55e0`, endpoints `0x02`/`0x82`; no MFi, no iAP2 |
| PC17 is a BOOT-detection pin, sampled at power-on, and is also USB D+ | CH32X035 datasheet (per the 2026-10-12 review); corroborated by maxgerhardt/ch32x035-arduino-usb-serial (resistor PC17-VCC) and wagiminator/CH32X035-USB-PD-Adapter (BOOT button); a WCH evaluation board documents a Download button on PC17 |
| X035 debug is WCH SDI on PC18/PC19, not ARM SWD | TinyUSB BSP `wch-riscv.cfg`: `adapter driver wlinke`, `transport select sdi` |
| Removing read protection can erase the user area | WCH documentation; wchisp issue #68 documents a real CH32X035F8U6 flashing failure caused by read protection |

## 2. Host-tool capability matrix (VERIFIED from the fetched sources)

| Capability | wchisp | chprog |
| --- | --- | --- |
| Default flash flow | identify, inspect config, erase, program, verify, reset (the "unprotect -> ..." text is a source comment; the CLI flash command does not unprotect) | detect (may rewrite protection config when flags indicate a protected device: destructive), erase, write, verify, reboot |
| Write base | hardcoded `address = 0x0`, no offset option | hardcoded `offset = 0`, no offset option |
| Erase semantics | `erase_code(sectors)` from the start, minimum sector count enforced | `erase(len)` with a minimum of 8 KB even for a tiny image: exactly the Clicks installer region |
| Code-flash readback | NOT implemented: `verify` is device-side comparison only | NOT implemented: same verify-only design |
| Config/EEPROM read | config registers and data EEPROM dump: YES | config read: YES |
| CH32X035 support | YES (device DB + X035-specific register handling) | YES (seven X035 variants in the device table) |

Consequences for the CK-5200:

1. A plain `flash` with our application-only V122 image (linked at `0x2000`)
   would ERASE AND OVERWRITE the Clicks installer at `0x0..0x2000`. The
   inspected local artifacts contain no installer backup. [CORRECTION] The
   earlier claim that no copy exists anywhere was unsupported. No available
   restore source is established for a lost installer.
2. The hoped-for "enter ISP, dump the installer" is not implementable with
   these tools today: neither implements code-flash readback. This does not
   prove that SDI is the only possible evidence source.
3. Even an apparent identification run deserves transcript review: chprog's
   `detect()` can perform a protection-config write on some device states.

## 3. The five questions (current answer states)

| # | Question | State |
| --- | --- | --- |
| 1 | Factory bootloader accessible after the app fails, CK-5200, no PCB access? | UNKNOWN, one live lead: the PC17 BOOT entry is family-verified. Whether the CK-5200's PC17 (USB D+) can be asserted safely at power-on without board modification is unanswered, and the presence of USB D+ is not permission to drive it externally. Software entry (`SystemReset_StartMode(Start_Mode_BOOT)` in the X035 SDK) requires a running application and is NOT independent recovery. |
| 2 | Boot-entry mechanism without PCB modification? | UNKNOWN. Candidates: PC17 strap (board routing unknown), blank-flash auto-ISP (presupposes erasure), software flag (not independent). |
| 3 | Clicks installer fallback independent of app/HID/iAP2? | UNKNOWN. A dump, matching implementation, or vendor documentation could supply evidence. The demonstrated A1/A2/A3/A0 service lives in the application (stock `0x3216`). An independent service would need separate executable code. |
| 4 | Recovery channel reachable from the iPhone? | No documented public API route was found for an ordinary iPhone app to the stated factory ISP. Installer fallback remains unknown. USBDriverKit supports macOS and M-series iPads, not iPhone in the documented hardware support. See [the 2026-10-14 Apple source check](iphone-recovery-transport.md). |
| 5 | Restore official stock without touching installer/config/serial/auth? | UNKNOWN end to end. Inspected application staging paths have bounded writes. The unread installer's copy bounds and device-specific preservation are not proven. Default ISP tools erase/write from offset zero and are unsuitable. |

## 4. Recovery-channel matrix per failure scenario

| Failure scenario | Clicks update dialect (needs a working app) | Factory USB ISP | SDI probe |
| --- | --- | --- | --- |
| Stock running, staging interrupted | Host harness demonstrates fresh A1/A2/A3 rewriting simulated staging. Actual installation remains unverified. | Default tool writes threaten the installer. | UNVERIFIED on this board. Not iPhone-only. |
| Custom app runs, has a fault | Available only if the application's USB and update service still execute. No physical custom restore is proven. | Entry and preservation remain UNKNOWN. | UNVERIFIED on this board. Not iPhone-only. |
| Custom app does not boot | NO | Board entry UNKNOWN. No supported public iPhone transport found. | Pad continuity, protection, and restoration remain UNVERIFIED. Not iPhone-only. |
| USB does not enumerate | NO | Board entry UNKNOWN. No supported public iPhone transport found. | Same unverified conditions. Not iPhone-only. |
| Presumed installer region destroyed | No demonstrated route. No inspected backup. | Does not supply the missing proprietary bytes. | A matching restore source and verified access would be necessary. Neither is established. |

## 5. Non-eliminable risks under the current constraints

- First live boot of the re-targeted CH32X035 firmware happens on the only
  keyboard (the dev-board rehearsal was consciously removed, 2026-10-12).
- The installer is unread until a dump route exists; every "what does the
  boot region do?" claim is about unread code.
- [CORRECTION] The earlier "recovery is Mac-only" conclusion was too broad.
  No independent CK-5200 recovery is demonstrated on any host. Apple's public
  USB driver support includes M-series iPads, but not a documented iPhone route.
- 100 percent reliability is not claimable through software tests alone. The
  demand is explicit failure coverage, independently verified implementation,
  and demonstrated behavior under interrupted or corrupted updates.

## 6. GO/NO-GO recommendation

**NO-GO for any flash of the CK-5200 at this time** (2026-10-12), for stock
re-flash and custom alike. The single condition that would change this: a
demonstrated, independently accessible recovery path. The highest-value
investigation, in order:

1. PC17 BOOT entry on the CK-5200: board routing, safe assertion at
   power-on, the case's power sequencing. A proposed live entry experiment
   requires your separate approval; no electrical manipulation happens on
   datasheet inference alone.
2. If PC17 is accessible: a read-only protocol transcript (wchisp-style
   identify/config-read only, no erase, no program, no config write) would
   establish chip identity and protection state without destruction. Code
   flash still cannot be dumped with these ISP tools, so this does NOT produce the
   installer backup.
3. If PC17 is not accessible: report the recovery requirement as UNMET. The
   SDI probe route was the historical full-dump lead and requires
   physical access decisions outside the current constraints. Photo
   update: labeled 3V3/SWD/GND/SWC pads beside U3 make this route
   concrete; continuity, pin assignment, and protection state remain
   unverified (hardware-photo-report.md).

A successful stock-to-stock update must never be used to override this
conclusion.

## 7. Photo evidence update (2026-10-13)

The 24 back-side PCB photographs are analyzed in
[hardware-photo-report.md](hardware-photo-report.md). Effects on this
report: U3 is a 48-lead QFP (12 leads per side, VERIFIED in five frames by
two models), so the CH32X035C8T6 (LQFP48) replaces the R8T6 (LQFP64M) as
the package hypothesis and that report's pin table applies (PC16=32,
PC17=33, PC18=34, PC19=37). Four labeled pads 3V3/SWD/GND/SWC sit beside
U3 (labels VERIFIED, two models; continuity UNVERIFIED). No BOOT pad
appears in any photo, so the PC17 questions stay UNKNOWN. The GO/NO-GO is
unchanged.

The SDI route is now concretized in
[recovery-sdi-probe.md](recovery-sdi-probe.md): the read-only tooling
(`wlink` supports CH32X035 identify, protection-state read, and a full
flash dump), the WCH-LinkE probe requirement, the unpowered continuity
procedure that must precede any probe session, and the hard prohibitions
(no erase, no write, no unprotect, no chprog). Nothing in it has been
executed.
