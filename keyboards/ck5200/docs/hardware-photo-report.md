# CK-5200 photo evidence report

Status: 2026-10-13. Subject: 24 back-side PCB photographs of the sole
CK-5200 unit, supplied by Rudi. Purpose: extract every hardware fact the
photos can support about the MCU, its debug and USB routing, and
non-destructive recovery. The analysis is read-only; no electrical contact
was made with the keyboard. Live flashing of any kind remains NO-GO.

Terminology follows the house standard: VERIFIED, PROBABLE, UNRESOLVED,
UNVERIFIED. No electrical connectivity is inferred from proximity,
silkscreen names, or trace fragments. Everything the photos cannot show is
listed as a gap, not guessed.

## 0. Summary

1. U3 is a 48-lead QFP package: 12 leads per side, counted complete in
   five independent frames (VERIFIED, two models). If U3 is a CH32X035,
   the variant is the C8T6 (LQFP48), not the R8T6 (LQFP64M). All
   previously published LQFP64M pin numbers (44/45/46/49) are superseded
   by the LQFP48 table in section 5.
2. U3's part number is unreadable in every frame. The only legible line
   reads best as `15PM` (P and M solid; the first two characters read as
   1 and 5 by four passes, called worn and ambiguous by the fifth).
   `15PM` is not a recognizable WCH part number, so the exact variant
   stays UNVERIFIED at the marking level.
3. Four round gold pads beside U3 carry the labels `3V3 / SWD / GND /
   SWC` (VERIFIED, two models). They are the strongest candidate on the
   board for the WCH two-wire SDI interface (PC18/PC19), but pad-to-pin
   continuity is UNVERIFIED: partial traces from the SWD and SWC pads
   run toward U3's left corner and disappear in shadow and solder; no
   trace reaches an identifiable lead.
4. No BOOT pad, no reset pad, and no button appear in any photo. The
   PC17 BOOT-entry route on this board remains UNKNOWN.
5. No battery is in frame. Power architecture remains UNKNOWN.
6. No serial number, barcode, or QR code was found in any photo, and no
   GPS EXIF on any checked file. Hands and part of a gold ring are
   visible in photos 03, 08, 13, 14 (= IMG_0598, IMG_0595, IMG_0594,
   IMG_0599).
7. Recovery GO/NO-GO is unchanged: NO-GO for live action. The photo
   evidence upgrades the SDI probe route from "if the pads exist" to
   "labeled pads exist; continuity and protection state unverified".

## 1. Method and independence model

Two evidence copies exist per photo: Rudi's HEIC originals (4032x3024,
`IMG_0594..IMG_0617.heic`) and the paste-buffer JPEGs analyzed first
(2000x1500 or 1500x2000, `ck5200-photo-01..24.jpg`). The mapping in
section 2 was established by 32x32 grayscale content matching; it is a
bijection, VERIFIED.

| Pass | Agent / model | Context | Input | Role |
| --- | --- | --- | --- | --- |
| Blind 1-3 | GPT-6-Astra medium subagents, sessions `ses_ecfbd860fffeeVBVRW71OeRMOm`, `ses_ecfbd860bffeqYVN53nUbzXfBm`, `ses_ecfbd8602fferD0E5PL0hYi63c` | fresh, no device context, no expected findings | all 24 photos (JPEG copies) | discovery |
| Crop A | GPT-6-Astra medium, session `ses_ecf411bb1ffeROI6MYvnD7gsnw` | fresh | full-res crops: U3 marking, U3 pins | targeted reads |
| Crop B | GPT-6-Astra medium, session `ses_ecf411ba4ffekViG713OVZmtJT` | fresh | full-res crops: pads, USB labels, six pads | targeted reads |
| Crop C | GPT-6-Astra medium, session `ses_ecf689efaffeEqhyorR8M4jmEq` | fresh | full-res crops: U7/U1/U5/Q6 | targeted reads |
| Challenge | GPT-6-Astra medium, session `ses_ecf0e4903ffezDZcWdeZ1v18jC` | fresh, adversarial; independent counting before any claim was shown | 6 crops | verification of contested claims |
| Codex | codex CLI, GPT-6.1-Sol medium, herdr pane `w2D:p5`, agent `ck5200-vision` | fresh, adversarial | 7 crops | cross-model challenge |
| ChatGPT | Rudi's own analysis | independent | his exports of the same photos | cross-check only |

The blind passes contained no CH32X035, SDI, BOOT, or pad-label context,
so their agreement with Rudi's ChatGPT analysis is real independent
evidence, not prompt echo. Crop regions were cut from full-resolution
JPEG conversions of the HEICs (`sips` conversion; the HEICs themselves
were never modified) and are kept in `hardware-photos/crops/` for review.

Discarded, with reason:

- llama-3.2-11b-vision: its first pass reported expected findings it was
  never given evidence for (prompt contamination), and it read zero text
  in a blind control. All its output is discarded.
- Tesseract OCR 5.5.3, raw and preprocessed (2x upscale, autocontrast,
  thresholding): the stylized low-contrast silkscreen yields no usable
  characters. Not used as evidence.

Limitations, stated plainly:

- Every working vision pass is an OpenAI-lineage model (GPT-6-Astra or
  GPT-6.1-Sol). Agreement is cross-model but not cross-vendor. The only
  available non-OpenAI vision model failed its control (above).
- Transcription agreement is not electrical verification.
- Trace following is limited to the visible outer layer; inner-layer and
  concealed routing are unreachable by photo.
- Codex could not confirm DP / DM / GND in the USB block from the crops
  (glare); those labels rest on the GPT-6-Astra reads plus ChatGPT.

## 2. Filename mapping (VERIFIED, content-matched bijection)

| IMG (HEIC) | Analyzed copy | IMG (HEIC) | Analyzed copy |
| --- | --- | --- | --- |
| IMG_0594.heic | ck5200-photo-13.jpg | IMG_0606.heic | ck5200-photo-11.jpg |
| IMG_0595.heic | ck5200-photo-08.jpg | IMG_0607.heic | ck5200-photo-19.jpg |
| IMG_0596.heic | ck5200-photo-16.jpg | IMG_0608.heic | ck5200-photo-04.jpg |
| IMG_0597.heic | ck5200-photo-18.jpg | IMG_0609.heic | ck5200-photo-20.jpg |
| IMG_0598.heic | ck5200-photo-03.jpg | IMG_0610.heic | ck5200-photo-22.jpg |
| IMG_0599.heic | ck5200-photo-14.jpg | IMG_0611.heic | ck5200-photo-17.jpg |
| IMG_0600.heic | ck5200-photo-12.jpg | IMG_0612.heic | ck5200-photo-23.jpg |
| IMG_0601.heic | ck5200-photo-06.jpg | IMG_0613.heic | ck5200-photo-24.jpg |
| IMG_0602.heic | ck5200-photo-10.jpg | IMG_0614.heic | ck5200-photo-01.jpg |
| IMG_0603.heic | ck5200-photo-15.jpg | IMG_0615.heic | ck5200-photo-21.jpg |
| IMG_0604.heic | ck5200-photo-09.jpg | IMG_0616.heic | ck5200-photo-05.jpg |
| IMG_0605.heic | ck5200-photo-02.jpg | IMG_0617.heic | ck5200-photo-07.jpg |

## 3. Evidence matrix

| ID | Finding | Status | Deciding evidence |
| --- | --- | --- | --- |
| E1 | U3 is a 48-lead QFP, 12 leads per side | VERIFIED (photo level) | Blind: photo 09 "twelve lead positions per side, 48 total", photo 11 "48 lead positions", photo 12 same. Crops `u3-marking-p19/21/22/24` complete four-sided counts of 12; `u3-pins-p09` complete left and right rows of 12. Challenge: VERIFIED. Codex: VERIFIED, and REFUTES 16 per side. ChatGPT's ~16/side (64-pin) reading is refuted by both models. |
| E2 | U3 part number unreadable; only legible line best-read `15PM`; upper line visible but unreadable | VERIFIED that nothing more is legible; `15PM` transcription PROBABLE | Crop A: 1, 5, P certain, M uncertain. Challenge: all four certain in `u3-marking-p19`. Codex: `[1?][5?]PM`, P and M clearest, first two worn (5/S ambiguity). Upper line unreadable in every frame; character count not establishable. No logo identified. Circular impressions near corners: possible pin-1 mark, UNVERIFIED. |
| E3 | Four round gold pads beside U3, labels in sequence `3V3 / SWD / GND / SWC` | VERIFIED | Blind: photos 07, 19, 21, 24. Crop `pads-4-labels` per-character read. Challenge: VERIFIED in both pad crops. Codex: VERIFIED; discriminated SWD's final glyph (closed, D) from SWC's (open, C). ChatGPT agrees. |
| E4 | 3V3 and SWD pads have dark center holes; GND and SWC pads are solid | PROBABLE | Challenge and Crop B agree on hole pattern. |
| E5 | SWD and SWC have partial traces toward U3's left corner; no trace reaches an identifiable lead; 3V3 and GND traces unresolvable | Continuity UNVERIFIED; VERIFIED that the photos cannot prove more | `pads-4-traces`: challenge and codex independently followed SWC's path (leaves right rim, bends toward the upper end of U3's lower-left lead row, lost in reflective solder) and SWD's path (leaves upper-right rim, runs above the pad row toward U3's left corner, lost at body shadow). 3V3: possible short neck lost at the label area (challenge: uncertain; codex: unresolvable). GND: unresolvable. |
| E6 | USB label block beside the bottom metal shell: `VBUS_P`, `DP`, `DM`, `GND`, plus one disputed label | VBUS_P VERIFIED; DP/DM/GND PROBABLE; disputed label best-read `UD_A3`, `CC1` REFUTED | Blind: photos 05, 07 read VBUS_P/UD_A3/DP/DM/GND. Crops `usb-labels-p05/p07`. Codex: reads `U D _ [A?][3?]` and REFUTES CC1; could not verify both characters of DP/DM/GND (glare). ChatGPT read CC1; two models now read `UD_*`. Tail characters uncertain. |
| E7 | Six rectangular gold pads near the left flex connector: four solid, one 1-hole, one 2x2-hole; no labels resolved; no outgoing traces resolved | UNRESOLVED | Crops `six-pads-p05/p11`. |
| E8 | Ribbon labels `MAIN` / `TYPEC`; fragment `IP15-KB-TYPEC`; ribbon connects main board to the small iPhone-side board | VERIFIED (text level) | Blind: photos 03, 08, 13, 18. |
| E9 | U7 (3-lead + tab) marking: line 1 `?239A`, line 2 `2410-9?0` | UNRESOLVED | Crop C reads `B?239A` / `2410/9?0`; blind pass reads `6239A` / `X41050`. First character and line 2 disputed. [INFERENCE] Candidate power or charger IC; not decision-critical. |
| E10 | Q5 `A19T`, Q9 `.7002` | UNVERIFIED | Blind pass only (low-res); not confirmed at crop level. |
| E11 | No battery in any photo | VERIFIED (frame-level) | All 24 photos, blind pass. |
| E12 | No BOOT pad, no reset (RST) pad, no button | VERIFIED (frame-level) | All 24 photos, blind pass. Absence in photos is strong evidence, not proof of absence on the assembled device. |
| E13 | No serial number, barcode, QR code, or identifier-shaped text | VERIFIED (frame-level) | All 24 photos, blind pass. Caveat: many markings are unreadable, so absence is evidenced but not certifiable. |
| E14 | No GPS EXIF | VERIFIED on checked files | `mdls kMDItemLatitude/kMDItemLongitude` null on IMG_0594, IMG_0600, IMG_0617, ck5200-photo-01, ck5200-photo-21. |
| E15 | Hands, fingers, and part of a gold ring visible | VERIFIED | Blind pass: photos 03, 08, 13, 14 (= IMG_0598, IMG_0595, IMG_0594, IMG_0599). |

## 4. The four pads beside U3 and the SDI hypothesis

Four round gold pads sit beside U3, labeled in sequence `3V3 / SWD / GND /
SWC` (top-to-bottom in the full-frame photos 21 and 24; the crops are
rotated). 3V3 and SWD have center holes; GND and SWC are solid (E3, E4).

Trace evidence (E5): two partial paths run toward U3's left corner region
(SWD from the pad row's upper side, SWC bending toward the lower-left lead
row), both lost in shadow and solder before reaching an identifiable lead.
3V3 and GND reveal no followable outgoing trace.

[INFERENCE] The pad names use the ARM SWD convention (SWD/SWC ~
SWDIO/SWCLK), which WCH hardware does not use: CH32X035 debug is the WCH
two-wire SDI on PC18 (DIO) and PC19 (DCK), live at reset, needing no
running application. If these pads are that pair, a WCH-Link-class probe
could attach with no PCB modification, and the interface works independent
of flash contents. The board silkscreen using ARM-style names does not
make it an ARM SWD port and does not prove the mapping.

What the photos do NOT establish:

- Pad-to-pin continuity for any pad (no trace reaches an identifiable
  lead).
- Which pad is DIO and which is DCK.
- The read-protection state (RDPR option byte; 0xA5 = unprotected;
  unprotect mass-erases, per recovery-research.md).
- That the pads were left enabled by the Clicks firmware: SDI is enabled
  by default after reset per WCH documentation, but protection may still
  block reads.

What would confirm or refute, cheapest first, all gated on Rudi's
approval:

1. Unpowered continuity measurement from each pad to U3 leads 34/37
   (C8T6 numbering, section 5), after identifying pin-1 orientation.
   Probing the board is physical contact even when unpowered, so it needs
   explicit approval.
2. A WCH-LinkE probe: read-only identify plus config read to learn the
   protection state. New hardware, outside the current no-new-hardware
   constraint. chprog must NOT be used first: `detect()` may rewrite the
   protection config on X035 (recovery-research.md section 2).

## 5. Package, pin map, and the C8T6 consequence

E1 makes the package a 48-lead QFP. If the CH32X035 family identification
(firmware evidence, X035-PORT.md) holds, the part is the CH32X035C8T6,
and these package pin numbers apply:

| Pad | Key functions | C8T6 (LQFP48) | R8T6 (LQFP64M) |
| --- | --- | --- | --- |
| PC16 | UDM (USB D-) | 32 | 44 |
| PC17 | UDP (USB D+), BOOT detect | 33 | 45 |
| PC18 | DIO (SDI data) | 34 | 46 |
| PC19 | DCK (SDI clock) | 37 | 49 |
| GND | | 47 | 31/63 |
| VDD | | 48 | 32/64 |

Source: ch32-riscv-ug CH32X035 pin map (fetched 2026-10-13). The table
internally corroborates the family erratum: on C8T6, PC10 and PC17 both
map to package pin 33, and PC11 and PC16 both map to package pin 32,
matching the documented internal bonding.

Family errata that bind the C8T6 hypothesis:

- The PC10/PC17 and PC11/PC16 pairs are internally bonded; both IOs of a
  pair must not be outputs at the same time. This applies to the CH32X035
  except F8U6 and D8U6, and the C8T6 is not excepted. Consequence:
  externally asserting PC17 (the BOOT-entry idea) is acting on a bonded
  pair, and the earlier caution stands.
- PC16/PC17 belong to the USB PHY; while `USB_PHY_V33` is set, open-drain
  output modes do not release the line, so external I2C-style assertion
  on those pads is unsafe.

The marking: WCH parts normally carry the part number on the first
marking line. Here the first line is unreadable (E2), so photos cannot
carry the identification past "48-lead QFP consistent with C8T6". `15PM`
[INFERENCE] may be a lot or date code; it is not used for identification.

## 6. USB and connectors

- Bottom-center metal-shell connector (J3 in earlier notes): the
  `VBUS_P / DP / DM / GND` block sits beside it. VBUS_P is verified; the
  disputed fourth label is `UD_*` with tail characters uncertain (E6);
  the CC1 reading is refuted. [INFERENCE] UD may denote USB data
  (UDM/UDP); do not rely on this.
- Six rectangular gold pads near the left connector: unlabeled, untraced
  (E7).
- The `MAIN`/`TYPEC` ribbon connects the main board to the small board
  carrying the iPhone-side plug (`IP15-KB-TYPEC`, E8).
- NOT established: which connector carries the MCU's USB data, what sits
  between any connector and PC16/PC17 (series resistors, ESD arrays,
  chokes), and whether the case's external USB-C reaches the MCU at all.
  The case's external USB-C is documented as charging passthrough; a Mac
  plugged there is NOT presumed to reach the MCU.

## 7. Power, reset, BOOT

- No battery is in frame (E11). The case battery may sit on the other
  side of the assembly; the photos cannot decide whether the MCU rail is
  battery-backed, so whether a power-on reset occurs on host attach stays
  UNKNOWN.
- `3V3` pad: a labeled power test point (E3).
- U7 (3-lead + tab, `?239A`): candidate regulator or charger IC,
  UNRESOLVED (E9).
- No reset or BOOT pad anywhere, no button (E12). The PC17 BOOT-entry
  route from recovery-research.md questions 1 and 2 stays UNKNOWN on
  this board.

## 8. Answers to the standing questions

The five questions from `.stock/photos/README.md`:

1. Exact MCU marking and package: package 48-lead QFP VERIFIED; marking
   beyond `15PM` unreadable; C8T6 if the X035 family holds. The PC10/PC17
   bonding question resolves by variant: C8T6 is not excepted, bonding
   applies. SDI pins exist on the package; labeled pads exist beside U3;
   continuity unverified.
2. USB data path: NOT established (section 6).
3. Power architecture: NOT established (section 7).
4. Exposed test pads, buttons, unpopulated footprints: four labeled pads
   (3V3/SWD/GND/SWC) VERIFIED; six unlabeled pads near the left
   connector; NO BOOT pad. The outcome that would have changed recovery
   feasibility did not materialize.
5. Connector types: bottom metal shell in the charging-passthrough area
   (USB label block beside it); left flex connector with the MAIN/TYPEC
   ribbon to the iPhone-side board; upper connector area carries the six
   pads.

Recovery-research.md question updates (its section 3): Q1/Q2 (boot entry
without the app) stay UNKNOWN; no BOOT pad exists in the photos. The SDI
scenario-matrix row moves from "if the pads exist" to "labeled pads exist;
continuity and protection unverified".

## 9. Privacy scan

- No serial number, barcode, QR code, or identifier-shaped text found in
  any of the 24 photos (E13). Caveat: many chip and board markings are
  unreadable, so absence is evidenced but not certifiable.
- No GPS EXIF on the checked HEICs and JPEGs (E14).
- Hands, fingers, and part of a gold ring are visible in photos 03, 08,
  13, 14 (= IMG_0598, IMG_0595, IMG_0594, IMG_0599). Publishing those
  four is a personal-privacy decision, not a device-privacy one. The
  crops directory contains board regions only.

## 10. Recovery GO/NO-GO

NO-GO for any live action on the CK-5200 (unchanged; recovery-research.md
section 6). What the photos changed:

| Route | Before photos | After photos |
| --- | --- | --- |
| SDI probe | "If the pads exist and are unprotected" | Labeled 3V3/SWD/GND/SWC pads exist beside U3 (labels VERIFIED); continuity, DIO/DCK assignment, and protection state UNVERIFIED; still needs probe hardware and explicit approval |
| PC17 BOOT entry | Unknown whether accessible | Still UNKNOWN; no BOOT pad found in any photo |
| Factory USB ISP | Entry mechanism unknown | Still UNKNOWN; connector-to-MCU USB routing not established |

Highest-value next steps, none performed and all gated on approval:
(1) unpowered continuity measurement from the four pads to U3;
(2) WCH-LinkE purchase decision; (3) read-only SDI identify and config
read, never chprog first.

## 11. Unresolved

1. U3 exact part number and manufacturer (marking unreadable).
2. Pad-to-pin continuity for all four pads; DIO/DCK assignment.
3. Read-protection state (RDPR).
4. Which connector carries MCU USB; components between connector and
   PC16/PC17.
5. Power architecture: battery backing, power-on reset on host attach.
6. Purpose of the six unlabeled rectangular pads.
7. U7, U1, U5 markings; Q5/Q9 markings (blind-only reads).
8. Tail and meaning of the `UD_*` label.
9. Pin-1 orientation of U3 (needed before any continuity mapping).
