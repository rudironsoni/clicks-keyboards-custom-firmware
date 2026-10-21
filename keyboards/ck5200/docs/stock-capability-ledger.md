# CK-5200 stock capability ledger

Status: **STATIC EVIDENCE.** Compiled 2026-10-21 from the V122 disassembly
(the [decode record](STOCK_DECODE.md) plus the settings-protocol pass), the
ClicksFT command table (commit `f93111b`), and the accepted physical getter
baseline. Purpose: classify what every physical matrix position can and
cannot do on stock firmware, so that capability decisions never assume
features the firmware does not implement. No arbitrary-key remapping
facility exists in V122; any claim below is tied to a handler, a builder
address, or an observed device response.

The 36 positions are the six matrix bytes (rows 0–5) × six bits
(`0x01,0x02,0x04,0x08,0x10,0x20`). Base/SYM/cursor values come from the
HID builder `0x2B1C–0x3010` as decoded in [STOCK_DECODE.md](STOCK_DECODE.md);
classification adds the configuration inputs proven in the
[settings protocol](stock-settings-protocol.md) pass.

## 1. Classification legend

- **Fixed**: HID usage compiled into the builder as an instruction
  immediate. Not configurable by any V122 command.
- **Mode-dependent**: output changes with a runtime mode (SYM layer hold,
  cursor layer, sticky-companion state) selected on-device.
- **Config-driven**: output depends on a persistent setting reachable over
  USB (`ctx+0x11E` layout, `ctx+0x11F` currency, `ctx+0x120` flags).
- **Consumer/special**: non-keyboard HID usage (consumer page) or a
  special crossing with dedicated builder logic.

## 2. The 36 positions

| Pos (r/b) | Base | SYM | Cursor | Class | Config input | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 0/01 | q | 1 | – | Fixed + mode (SYM digit) | – | builder immediates; [decode table](STOCK_DECODE.md#keymap-stock-0x2b1c0x3010) |
| 0/02 | u | 7 | – | Fixed + mode | – | same |
| 0/04 | k | ' | Down | Fixed + mode (cursor) | – | same |
| 0/08 | Backspace | – | – | Fixed | – | same |
| 0/10 | Enter | – | – | Fixed (special-cased at `0x2E28–0x2E44` per decode) | – | same |
| 0/20 | – | – | – | Consumer (bit 2) | – | consumer usage 3 of the decode record |
| 1/01 | w | 2 | Up | Fixed + mode (cursor: WASD/IJKL under Cursor Mode, flag bit 7) | `ctx+0x120` bit 7 | decode table; flag semantics |
| 1/02 | i | 8 | Up | Fixed + mode | `ctx+0x120` bit 7 | same |
| 1/04 | d | ; | Right | Fixed + mode | – | decode table |
| 1/08 | l | ' | Right | Fixed + mode | – | decode table |
| 1/10 | v | / | – | Fixed + mode | – | decode table |
| 1/20 | – | – | – | Consumer (bit 1), chords with 5/10 | – | decode record |
| 2/01 | e | 3 | – | Fixed + mode | – | decode table |
| 2/02 | o | 9 | – | Fixed + mode | – | decode table |
| 2/04 | f | ; | – | Fixed + mode | – | decode table |
| 2/08 | – | – | – | **Unused** (spare crossing) | – | decode record: "none, spare crossing" |
| 2/10 | b | 1 | – | Fixed + mode | – | decode table |
| 2/20 | – | – | – | Special: Left GUI (Command) | – | decode record |
| 3/01 | r | 4 | – | Fixed + mode | – | decode table |
| 3/02 | p | 0 | – | Fixed + mode | – | decode table |
| 3/04 | g | 9 | – | Fixed + mode | – | decode table |
| 3/08 | z | ' (configurable) | – | Fixed + mode + **config-driven** | `ctx+0x11E` layout (shift interplay at `0x2DF4–0x2DF8`) | decode record; `lbu a7, 0x11e` in builder |
| 3/10 | n | , | – | Fixed + mode | – | decode table |
| 3/20 | Space | – | – | Fixed (suppressed when 5/10 held, `0x2E28–0x2E44`) | – | decode record |
| 4/01 | t | 5 | – | Fixed + mode | – | decode table |
| 4/02 | a | - | Left | Fixed + mode | – | decode table |
| 4/04 | h | 0 | – | Fixed + mode | – | decode table |
| 4/08 | x | 7 | – | Fixed + mode | – | decode table |
| 4/10 | m | . | – | Fixed + mode | – | decode table |
| 4/20 | – | – | – | Special: Left Ctrl | – | decode record |
| 5/01 | y | 6 | – | Fixed + mode | – | decode table |
| 5/02 | s | / | Down | Fixed + mode | – | decode table |
| 5/04 | j | Clicks key | Left | **Config-driven behavior key** | `ctx+0x120` bit 6 (Tab vs Control, tested at `0x2EDE–0x2EEE`); currency-glyph path enters at `0x2F32–0x2F7C` | decode record; builder flags test |
| 5/08 | c | 2 | – | Fixed + mode | – | decode table |
| 5/10 | sticky companion (one-shot SYM mechanics `0x2876`) | – | – | Mode toggle key | – | decode record |
| 5/20 | – | – | – | Consumer (bit 0) | – | decode record |

## 3. Config-driven behaviors (the complete stock-configurable set)

| Behavior | Control | USB command | Builder evidence |
| --- | --- | --- | --- |
| Clicks key = Tab or Control | flags bit 6 (mask `0x40`) | `0x83` | `0x2EDE–0x2EEE`: `lbu a0, 0x120; andi 0x40` gates the Control emission |
| Cursor Mode (WASD/IJKL→arrows, `123+CMD` entry per Clicks v1.2 guide) | flags bit 7 (mask `0x80`) | `0x83` | cursor-column entries of the decode table + flag semantics |
| Alt Sticky, Shift Sticky, Backlight on, Soft Return, Accessory Stop, Backlight Shortcut | flags bits 0–5 | `0x83` | handler `0x3472–0x358A` (see protocol §3.2) |
| Backlight brightness (persist/temp) | `ctx+0x11D` | `0x81/0x82/0x84` | PWM `0x3010`; store `0x343A` |
| Backlight timeout | `ctx+0x124` | `0x85/0x86` | `0x35FC–0x3622` |
| Accessory idle interval | `ctx+0x126` | `0x87/0x88` | `0x365E–0x36B4` |
| Currency glyph ($ ¢ £ € ¥/RMB) | `ctx+0x11F` (+ layout for £/€ variants) | `0x89/0x8A` | five-way switch `0x2F7C–0x300A` |
| US/UK layout (symbol generation) | `ctx+0x11E` | `0x8B/0x8C` | `0x2DF8`, `0x2FAA`, `0x2FC2` builder reads |

### The currency emission path, decoded

`0x2F7C–0x300A` switches on `ctx+0x11F` and emits Mac-standard
modifier+usage pairs, with UK/US variants from `ctx+0x11E`:

| Currency | Value | Emission (V122) | Mac-standard gesture |
| --- | --- | --- | --- |
| Dollar | 0 | usage `0x21` ('4') with shift (`0x2F92` path) | ⇧4 |
| Cent | 1 | modifier `0x04` + usage `0x21` (`0x3006–0x3008`) | ⌥4 |
| Pound | 2 | UK: shift + usage `0x20` ('3', `0x2FC2–0x2FD0`); US: `0x04` + `0x20` (`0x3000–0x3004`) | ⇧3 (UK) / ⌥3 (US) |
| Euro | 3 | UK: `0x04` + usage `0x1F` ('2', `0x2FB6–0x2FBA`); US: `0x06` + `0x1F` (`0x2FBE`) | ⌥2 (UK) / ⇧⌥2 (US) |
| RMB/Yen | 4 | `0x04` + usage `0x1C` ('y', `0x2FA4–0x2FA8`) | ⌥Y |

This is a device-side emission of fixed gestures, not a per-key remapping
table: one builder path produces all five glyphs.

## 4. The hard boundary

V122 constructs its HID report through a large conditional chain
(`0x2B1C–0x3010`) with letter/symbol usages compiled as immediates. The
configuration inputs above are the only runtime-selected behaviors. There is
no per-key usage table, no write command for key assignments, and no
command that stores a keycode for any matrix position. Consequences:

- Arbitrary "any key → any HID usage" remapping is **not implementable on
  unmodified stock V122**. That is a statement about the shipped firmware,
  not about the hardware: the correct full statement is that arbitrary
  remapping requires firmware change, reachable by two distinct routes:
  (1) a patched or extended OEM-derived application (offline modification
  of the V122 image, preserving the OEM USB/iAP2/authentication stack), or
  (2) replacement firmware such as the existing QMK port. Neither route is
  equivalent to configuring unmodified V122, and neither is authorized.
- The realistic stock ceiling is: the eight behavior flags, brightness and
  timing settings, currency/layout selection, native Cursor Mode, and the
  SYM layer — exactly what the official app exposes.
- Capabilities beyond that (arbitrary assignments, tap-hold, combos, layers,
  device-persistent custom keymaps) fall to the firmware-change routes
  above; their feasibility is a separate investigation.

## 5. iOS-assisted extensions (no firmware writes)

Per Clicks' published v1.2 guidance and the capability findings:

- Clicks key set to Control (`0x83` mask `0x40`), then iOS Settings →
  Hardware Keyboard → Modifier Keys provides a host-side remap layer.
- iOS Full Keyboard Access + Shortcuts can bind physical keys to actions
  (executed by iOS, not new HID usages from the keyboard).
- These layers must be behavior-tested in the target apps; a Shortcut is
  not a physical Escape/Undo HID event in every application.

## 6. Open items

1. The currency-glyph trigger gesture: the emission path is entered from the
   matrix row-5 bit-2 block (`0x2F32–0x2F42`) under a state-byte condition
   (`ctx+0x0` bit 4), i.e. tied to the Clicks key crossing and a runtime
   state not yet decoded (candidate: the SYM/companion state machine). The
   five-way switch itself is fully decoded; the exact user-facing gesture
   that reaches it is the open part.
2. Runtime status bits (0x8D, state word bits 4–5): what sets them.
3. The three opaque record bytes (`ctx+0x121..0x123`): no reader found in
   V122; no USB getter.
4. The `[UNVERIFIED]` SYM-semantics items carried over from the decode
   record remain open.

## 7. Verification status

| Claim | Status |
| --- | --- |
| Handler table, fields, persistence record | VERIFIED statically (V122 instructions, addresses above) |
| Flag-bit semantics | VERIFIED (V122 + ClicksFT + observed `0xA7`-consistent defaults) |
| Currency/layout enumerations | VERIFIED (V122 switch + ClicksFT + observed reads `00`/`21`) |
| Zero-mask 0x83 read path | STATICALLY PROVEN, not executed ([protocol §3.2](stock-settings-protocol.md#32-0x83--the-eight-behavior-flags)) |
| Physical getter baseline | VERIFIED on device (2026-10-14 accepted baseline) |
| Any write or query beyond that | NOT EXECUTED, NOT AUTHORIZED |
