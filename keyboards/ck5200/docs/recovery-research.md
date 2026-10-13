# Recovery research: existing mechanisms only, evidence-cited

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
   installer bytes exist nowhere except on the one chip: no artifact on any
   machine contains them (verified by the 2026-10-11 review). The loss would
   be irreversible.
2. The hoped-for "enter ISP, dump the installer" is not implementable with
   these tools today: neither can read code flash. The remaining dump routes
   are a WCH-Link/SDI probe or none.
3. Even an apparent identification run deserves transcript review: chprog's
   `detect()` can perform a protection-config write on some device states.

## 3. The five questions (current answer states)

| # | Question | State |
| --- | --- | --- |
| 1 | Factory bootloader accessible after the app fails, CK-5200, no PCB access? | UNKNOWN, one live lead: the PC17 BOOT entry is family-verified. Whether the CK-5200's PC17 (USB D+) can be asserted safely at power-on without board modification is unanswered, and the presence of USB D+ is not permission to drive it externally. Software entry (`SystemReset_StartMode(Start_Mode_BOOT)` in the X035 SDK) requires a running application and is NOT independent recovery. |
| 2 | Boot-entry mechanism without PCB modification? | UNKNOWN. Candidates: PC17 strap (board routing unknown), blank-flash auto-ISP (presupposes erasure), software flag (not independent). |
| 3 | Clicks installer fallback independent of app/HID/iAP2? | UNKNOWN until the installer at `0x0..0x2000` is read. The A1/A2/A3/A0 dialect lives in the application (stock `0x3216`), not the installer, so the installer must contain separate, unseen code to be a fallback. |
| 4 | Recovery channel reachable from the iPhone? | Factory ISP: NO (raw USB vendor device; Apple's External Accessory requires MFi, and the published raw-USB entitlement is macOS-only per Apple DTS, verified by the 2026-10-12 review). Installer fallback: unknown, nothing suggests iAP2. Working answer: recovery is Mac-only. |
| 5 | Restore official stock without touching installer/config/serial/auth? | Via the Clicks dialect: YES by design (stock's own writer never writes below `0x2000`; staging is `0x8A00`+; serial `0xF600` and config `0xF700` are outside every write path). Via factory ISP: NO with default tools (offset-0 erase/write). |

## 4. Recovery-channel matrix per failure scenario

| Failure scenario | Clicks update dialect (needs a working app) | Factory USB ISP | SDI probe |
| --- | --- | --- | --- |
| Stock running, update interrupted | Recovers: fresh A1/A2/A3 rewrites staging (VERIFIED in the restore harness) | Would destroy the installer | Readback-verified restore possible |
| Custom app runs, has a fault | Recovers: A1/A2/A3/A0 retained in the port; software `Start_Mode_BOOT` is an orderly exit, not rescue | Entry requires the app or a strap (unknown) | Yes |
| Custom app does not boot | NO (protocol needs USB enumerating) | Only if PC17 entry is accessible on this board: UNKNOWN; no BOOT pad appears in the 2026-10-13 photos | Yes, if the labeled 3V3/SWD/GND/SWC pads beside U3 are the SDI pair: continuity and protection state UNVERIFIED (2026-10-13 photos) |
| USB does not enumerate | NO | Only via PC17 entry: UNKNOWN; no BOOT pad appears in the 2026-10-13 photos | Yes, same conditions |
| Installer region destroyed | Nothing recovers it: no artifact contains the installer bytes | Would brick the vendor update path permanently | Only a pre-existing dump could |

## 5. Non-eliminable risks under the current constraints

- First live boot of the re-targeted CH32X035 firmware happens on the only
  keyboard (the dev-board rehearsal was consciously removed, 2026-10-12).
- The installer is unread until a dump route exists; every "what does the
  boot region do?" claim is about unread code.
- The iPhone cannot reach the factory ISP; emergency recovery is Mac-only
  even in the best case.
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
   flash still cannot be dumped over ISP, so this does NOT produce the
   installer backup.
3. If PC17 is not accessible: report the recovery requirement as UNMET. The
   SDI probe route remains as the only full-dump option and requires
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
