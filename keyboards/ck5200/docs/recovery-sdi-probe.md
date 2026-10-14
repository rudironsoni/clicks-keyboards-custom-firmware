# SDI probe runbook: the read-only recovery path

Status: 2026-10-14. This runbook makes the SDI probe route concrete. It is
preparation only: nothing in it has been executed, every step that touches
the keyboard requires Rudi's explicit approval beforehand, and live
flashing of any kind remains NO-GO (recovery-research.md section 6).

Route summary: the photo report (hardware-photo-report.md) verified four
labeled pads beside U3, `3V3 / SWD / GND / SWC`. WCH's own Link manual
(V2.3, table 6) lists the CH32X035 debug pair as SWDIO=PC18, SWCLK=PC19,
the third independent source for that mapping after the datasheet note
and the ch32-riscv-ug pin map. If the pads are that pair, a WCH-LinkE can
attach without PCB modification, and the unread installer at
`0x0..0x2000` finally becomes dumpable, read-only.

## Prerequisites

1. Multimeter with continuity/resistance mode (Phase A, Rudi's hands).
2. WCH-LinkE probe (CH32V305-based; the ch32-rs recommended probe).
   Sources: WCH official, Olimex, Tindie (MuseLab), AliExpress listing
   1005005180653105. Inexpensive; verify price at purchase time.
3. macOS host with `wlink` installed (Phase B):
   `cargo install wlink` or a prebuilt binary from the ch32-rs/wlink
   GitHub releases; `brew install libusb` if the cargo build needs it.

## Phase A: unpowered continuity verification

Purpose: turn the pad-to-pin hypothesis into a verified continuity map
before any probe is connected. Multimeter only; the keyboard stays
unpowered throughout Phase A.

Precautions: disconnect every power source (case battery if separable,
iPhone, cable). Work ESD-aware (touch grounded metal before the board).
Never probe a powered board.

A1. Fix the pin-1 reference. LQFP pin 1 sits at the corner with the
    mark; numbering runs counterclockwise. The photos show a circular
    impression near one corner of U3 [UNVERIFIED as the pin-1 mark].
    Set the reference so that package pin 32 (PC16), 33 (PC17), 34
    (PC18), 37 (PC19), 47 (GND), 48 (VDD) can be located per the table
    in hardware-photo-report.md section 5.

A2. Sanity anchors first. Measure resistance (record ohms, not just
    continuity-beep):
    - `3V3` pad to U3 pin 48 (VDD): expected near 0 ohm, or a
      decoupling-network reading.
    - `GND` pad to U3 pin 47 (GND): expected near 0 ohm.
    If either anchor fails, the pin-1 reference or the method is wrong:
    STOP and re-derive; do not reinterpret the data pins.

A3. The two hypothesis measurements:
    - `SWD` pad to U3 pin 34 (PC18/DIO).
    - `SWC` pad to U3 pin 37 (PC19/DCK).
    Record both resistances and note any diode-drop behavior (meter in
    diode mode both directions if the ohm readings are not clean).

A4. Decision:

| Result | Meaning | Next |
| --- | --- | --- |
| Both SWD-pin34 and SWC-pin37 near 0 ohm | SDI pair CONFIRMED at continuity level | Phase B is meaningful; proceed on approval |
| Continuity to different pins | Pads are not the SDI pair, or pin-1 reference wrong | Re-check A1/A2 first, then record as REFUTED for this route |
| No continuity to any U3 pin | Hidden routing or wrong hypothesis | Route UNMET at photo+multimeter level |

Record every reading in a note; those readings are the evidence artifact
for this phase. Continuity is electrical verification; it is still not a
guarantee that SDI is enabled or unprotected, which only Phase B answers.

## Phase B: read-only probe session

Run only after Phase A confirms the pair AND Rudi approves the session.

Wiring: probe SWDIO to the `SWD` pad, SWCLK to the `SWC` pad, GND to
`GND`. Do NOT enable the probe's 3.3V/5V power output in the first
session: the board has its own power path, and back-feeding its rail is
an unapproved electrical change. Power the keyboard the way it is
normally powered and connect signals plus ground only. [UNVERIFIED] how
the board powers on the bench without the case battery or iPhone
attached; resolve this before the session (this is the power-architecture
gap from the photo report).

Commands (wlink, read-only set; capture everything with `tee`):

1. `wlink -vv probe | tee sdi-identify.txt`
   Identify: expected chip line `CH32X035 [CH32X035C8T6]` (exact variant
   per the chip's reported ID), plus `Flash protected: true/false`.
   This alone closes the C8T6 question and the protection question.
2. If, and only if, protection reads as false:
   `wlink -vv dump 0x08000000 63488 > ck5200-flash-dump.bin`
   Reads all 62 KiB of code flash, including the installer at
   `0x0..0x2000` and the application. This dump is the first complete
   backup of the only unit's firmware.

Hard prohibitions for the session:

- NO `wlink flash`, `wlink erase`, `wlink protect`, `wlink unprotect`:
  write and erase operations are out of scope; unprotect erases flash.
- NO chprog anywhere near this board: its `detect()` can rewrite the
  protection config on the X035 family (recovery-research.md section 2).
- NO probe-firmware updates: the `wlink-iap` updater has bricked probes
  in the field (community report); the LinkE works out of the box.
- If identify fails or shows `protected: true`: STOP, save the
  transcript, change nothing. Unprotect would mass-erase, destroying
  the installer; that decision does not exist in this runbook.

Known compatibility caveat: newer LinkE probe firmware (v2.12+) has a
community report of freezing `wlink` mid-operation. If attach fails with
a timeout, the fallback is OpenOCD with `adapter driver wlinke` /
`transport select sdi` (the TinyUSB BSP `wch-riscv.cfg` path already on
record), or a probe running the older firmware. Do not flash the probe
to "fix" it (prohibition above).

Secondary attach risk, from the WCH manual's own troubleshooting notes:
if the running Clicks application multiplexes PC18/PC19 as GPIO, attach
can fail while the app runs. SDI is the default after reset, so
attach-under-reset (wlink halt/reset) remains the retry path; if that
also fails, record it as evidence the app disabled debug, which is
itself a finding.

## Evidence handling

- The flash dump contains Clicks' proprietary installer and firmware.
  It goes under `.stock/` (gitignored), same policy as the stock images;
  never committed. Its SHA-256 gets recorded in the private notes.
- Transcripts (`sdi-identify.txt`) may be committed after review: they
  contain chip IDs and protection state, no vendor code.
- Phase A resistance notes go into the recovery evidence docs.

## What success changes, and what it does not

| Outcome | Changes |
| --- | --- |
| Identify as CH32X035C8T6, unprotected | C8T6 hypothesis closes; protection question closes |
| Full 62 KiB dump taken | The installer at 0x0..0x2000 is finally backed up: a destroyed installer stops being an unrecoverable brick (a future ISP accident becomes recoverable by reflashing the dump through the same probe) |
| Identify shows protected: true | The dump route is dead unless someone accepts a mass-erase; the recovery requirement for this route is UNMET |
| Attach fails entirely | The SDI route is UNMET at this hardware; report it honestly |

A successful dump does NOT by itself unlock custom flashing. The custom
firmware gate (flash-readiness.md authorization boundary) still requires
the demonstrated independent recovery path to be in place end to end
first: the dump is the enabler that makes such a demonstration
reversible, which is exactly why this runbook is read-only.

## Open items before Phase A can run

1. Rudi confirms he has a multimeter (or buys one).
2. Approval statement for Phase A (unpowered continuity on the board).
3. Phase B additionally needs the WCH-LinkE in hand and the bench-power
   question answered (how the board powers outside the case).
