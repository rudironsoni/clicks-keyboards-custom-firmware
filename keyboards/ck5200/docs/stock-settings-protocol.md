# CK-5200 stock settings protocol: instruction-level contract

Status: **STATIC EVIDENCE. No command in this document has been executed
against the physical keyboard beyond the already-accepted read-only getter
baseline.** Compiled 2026-10-21 from the V122 disassembly, cross-validated
against ClicksFT and the accepted iPhone diagnostics. Part of the stock
configuration protocol milestone (Rudi's 2026-10-21 direction); M4 stays
closed; no physical writes are authorized by this document.

Inputs:

| Source | Pin |
| --- | --- |
| Stock V122 image | SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`, base `0x2000`, disassembled with LLVM `+xwchc` (see [STOCK_DECODE.md](STOCK_DECODE.md)) |
| ClicksFT command table | [`default_commands.xml`](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/app/src/main/res/raw/default_commands.xml) at commit `f93111b7434a34ead80604843204d062d1365c2c` |
| Observed device reads | Accepted baseline, 2026-10-14 ([flash-readiness.md](flash-readiness.md)) |

## 1. Command summary

All requests are companion-protocol frames `[total_length, command, args…]`
(see the [decode record](STOCK_DECODE.md#link-packets-byte-exact-from-0x542a)
and the M4 OEM-transition analysis). Replies are
`[length, 0x02, command, status, payload…]`, assembled at runtime context
`ctx+0x10C` and dispatched back through the `0x358A` return. `ctx` is the RAM
state object at `0x200000AC` (M4 finding). Status `0x00` is success; `0x01`
is the observed rejection status.

| Op | Purpose | Frame | Handler (V122) | Persists |
| --- | --- | --- | --- | --- |
| 0x81 | Set brightness, persistent | `[03, 81, val]` | `0x3422–0x3470` | Yes |
| 0x82 | Set brightness, temporary (PWM only) | `[03, 82, val]` | `0x3422–0x3446` (no store) | No |
| 0x83 | Masked behavior-flag update / read | `[04, 83, mask, bits]` | `0x3472–0x358A` | On change |
| 0x84 | Read configured brightness | `[02, 84]` | `0x35DA–0x35FA` | Read-only |
| 0x85 | Set backlight timeout (ms) | `[04, 85, hi, lo]` | `0x32BA–0x32E2`, `0x35FC–0x3622` | Yes |
| 0x86 | Read backlight timeout | `[02, 86]` | `0x3634–0x365C` | Read-only |
| 0x87 | Set accessory idle interval | `[04, 87, hi, lo]` | `0x365E–0x36B4` | Yes |
| 0x88 | Read accessory idle interval | `[02, 88]` | `0x36C6–0x36EE` | Read-only |
| 0x89 | Set currency symbol | `[03, 89, val]` | `0x32FC`, `0x3302–0x3328` | Yes |
| 0x8A | Read currency symbol | `[02, 8A]` | `0x3706–0x371A` | Read-only |
| 0x8B | Set keyboard layout region | `[03, 8B, val]` | `0x371C–0x3748` | Yes |
| 0x8C | Read keyboard layout region | `[02, 8C]` | `0x375C–0x3770` | Read-only |
| 0x8D | Read two runtime status bits | `[02, 8D]` | `0x333E–0x335C` | Read-only |

Observed baseline values (accepted read-only session, unchanged in three
sweeps): `0x84 → FF`, `0x86 → 4E 20` (20,000 ms), `0x88 → 00 B4` (raw 180),
`0x8A → 00` (dollar), `0x8C → 21` (US), `0x8D → 00`.

## 2. Runtime configuration fields

The settings live in the runtime context and mirror the 12-byte flash record
at `0x0800F700` (see [§4](#4-persistence-the-12-byte-flash-record)):

| Field | Offset | Set by | Read by |
| --- | --- | --- | --- |
| Brightness byte | `ctx+0x11D` | 0x81 (`0x343A`) | 0x84 (`0x35DC`) |
| Layout region | `ctx+0x11E` | 0x8B (`0x372C`) | 0x8C (`0x3768`) |
| Currency symbol | `ctx+0x11F` | 0x89 (`0x330C`) | 0x8A (`0x3712`) |
| Behavior flags | `ctx+0x120` | 0x83 (`0x348E` etc.) | 0x83 reply (`0x355E`) |
| Backlight timeout, LE16 | `ctx+0x124` | 0x85 (`0x3606`) | 0x86 (`0x3634`) |
| Accessory idle interval, LE16 | `ctx+0x126` | 0x87 (`0x3698`) | 0x88 (`0x36C6`) |
| Dirty flag | `ctx+0x0` bit 0 | every persistent setter | main-loop saver |

## 3. Per-command contract

### 3.1 0x81 / 0x82 — brightness

- Argument-count check: exactly one value byte (`0x3424`; otherwise reply
  `[04, 02, cmd, 01]`).
- `0x3428–0x342A`: the value is passed to the PWM driver at `0x3010` — the
  immediate brightness change applies for BOTH commands.
- `0x342C`: only when `cmd == 0x81` (the dispatcher pinned `s4 = 0x81`):
  store to `ctx+0x11D` **only if changed** (`0x3432–0x343A`) and set the
  dirty flag (`0x343E`). `0x82` jumps straight to the reply — no store, no
  dirty bit. **The temporary/persistent split is confirmed at instruction
  level.**
- Reply: `[04, 02, cmd, 00]`.
- Getter 0x84 reads `ctx+0x11D`, the *configured* value. A temporary 0x82
  adjustment is invisible to 0x84; validate 0x82 by observing the physical
  backlight, not the getter.

### 3.2 0x83 — the eight behavior flags

Request `[04, 83, mask, bits]`. Handler `0x3472–0x358A`:

- Argument-count check: exactly two bytes (`0x3474`; otherwise `[04, 02, 83, 01]`).
- The current flags byte is captured first: `lbu s2, 0x120` at `0x347A`.
- Eight per-bit blocks, each gated on `mask & bit`: value bit set → `ori`
  (set), value bit clear → `andi` complement (clear paths at
  `0x3594, 0x359A, 0x359E, 0x35A8, 0x35AC, 0x35B4, 0x35BA, 0x35C0`). Bits
  not selected by the mask are preserved.
- Bit-7 (CursorMode) uses signed byte tests (`lb`/`bgez`, `0x3542–0x355A`).
- Extra side effects inside 0x83: bit 2 (backlight) additionally sets the
  RAM state byte bit 2 and calls the backlight gate `0x3050(1, on/off)`
  (`0x34C4–0x34D0`, clear at `0x35A4`); bit 4 (Accessory Stop) sets state
  byte bit 3 on the set path only (`0x3502–0x3508`).
- Dirty condition (`0x355E–0x356C`): the final flags byte is compared with
  the captured original; **the dirty flag is set only when the byte actually
  changed.**
- Reply: `[05, 02, 83, 00, <final flags byte>]` (`0x356E–0x3586`).

Flag semantics (cross-validated V122 + ClicksFT + observed defaults):

| Bit | Mask | Function | Factory default (`0xA7`) |
| --- | --- | --- | --- |
| 0 | 0x01 | Alt Sticky / Alt Lock | on |
| 1 | 0x02 | Shift Sticky / Caps Lock | on |
| 2 | 0x04 | Backlight enabled | on |
| 3 | 0x08 | Soft Return | off |
| 4 | 0x10 | Accessory Stop / Battery Saving | off |
| 5 | 0x20 | Backlight Shortcut | on |
| 6 | 0x40 | Clicks Key: Control instead of Tab | off (Tab) |
| 7 | 0x80 | Cursor Mode | on |

`0xA7` = `1010 0111`: matches every setting state in the official app
screenshots.

**Zero-mask read (`04 83 00 00`), static proof.** With `mask = 0`:

1. Every per-bit block begins with a mask test that jumps over the block
   when the mask bit is clear (e.g. `0x347E–0x3480` for bit 0; the same
   pattern for all eight). With mask 0, all eight blocks are skipped, so
   no instruction writes `ctx+0x120`.
2. The bit-7 block's signed test `bgez mask → skip` (`0x3546`) also skips
   for mask 0.
3. The final byte therefore equals the captured original; the
   `beq a4, s2 → skip` at `0x3562` leaves the dirty flag untouched.

The reply is `[05, 02, 83, 00, <current flags>]` with no state change and no
dirty flag — a side-effect-free query of the complete flag byte through the
setter opcode. **[Static proof only: this frame is not in the approved
physical read allowlist and has not been executed on the device.]**

### 3.3 0x85 / 0x87 — timeouts

- 0x85 (backlight timeout, ms): two argument bytes required (`0x32BA`);
  value parsed big-endian (`0x32C0–0x32C8`). Values `<= 999` reply
  `[04, 02, 85, 01]` and store nothing — **minimum 1000 ms is enforced in
  firmware** (`0x32C8–0x32D0`). Valid values store to `ctx+0x124` as LE16
  when changed, set the dirty flag, and also set state-byte bits 0 and 2
  (`0x35FC–0x3610`); reply `[04, 02, 85, 00]`.
- 0x87 (accessory idle interval): two argument bytes required (`0x3660`);
  value big-endian. The firmware divides the current backlight timeout by
  1000 (`0x3670–0x3674`) and **rejects an idle interval below the timeout in
  seconds** with `[04, 02, 87, 01]` (`0x3678–0x368C`). Valid values store to
  `ctx+0x126` as LE16 when changed, set the dirty flag plus state-byte bits
  0 and 3 (`0x368E–0x36A2`); reply `[04, 02, 87, 00]`.
- The 0x86/0x88 getters return the LE16 fields as two big-endian payload
  bytes (`0x364C–0x365C`, `0x36DE–0x36EA`).

### 3.4 0x89 / 0x8B — currency and layout

- 0x89: exactly one value byte (`0x32FC`); stored to `ctx+0x11F` when
  changed, dirty flag set (`0x3302–0x3316`); reply `[04, 02, 89, 00]`.
- 0x8B: exactly one value byte (`0x371E`); stored to `ctx+0x11E` when
  changed, dirty flag set (`0x3722–0x3736`); reply `[04, 02, 8B, 00]`.
- **Neither setter validates its enumeration at the protocol level**: any
  byte is accepted and stored. The five-way currency switch and layout
  branches in the HID generator (see the
  [capability ledger](stock-capability-ledger.md)) define what happens for
  out-of-range values; nothing in the contract makes it safe. A client must
  restrict writes to the documented values: currency `0..4`, layout
  `0x20`/`0x21`.
- Getters 0x8A/0x8C reply `[05, 02, cmd, 00, value]` (`0x3706`, `0x375C`).

### 3.5 0x8D — event status

`0x333E–0x335C` reads two bits (4 and 5) of the 32-bit state word at
`ctx+0x0` and replies `[05, 02, 8D, 00, value]`. These are runtime status
bits, not the configuration flags (observed `0x00` in the baseline).

## 4. Persistence: the 12-byte flash record

- Loader `0x39A0–0x39E8`: on startup the application copies three words from
  `0x0800F700` into `ctx+0x11C..0x126` and requires the first byte to be
  `0x0C`; otherwise it loads factory defaults
  (`0C 80 21 00 A7 00 00 00 88 13 58 02`).
- Saver: the main loop (`0x238E`) calls `0x39FA` every iteration; when the
  dirty bit is set it calls the read-modify-write helper `0x31B2` with
  `address 0x0800F700`, `length 12` (`0x3A0A–0x3A18`), then clears the bit.
  `0x31B2` reads the full 256-byte page `[0xF700,0xF800)`, overlays the 12
  bytes, erases the whole page through the fast-erase writer `0x3174`
  (CH32X035 `FTER`/`FTPG`, 256-byte granularity), and programs it back.
- Record layout (little-endian where marked):

```text
0x0800F700: 0C        record marker (validity tag)
            80        brightness            -> ctx+0x11D  (0x81/0x84)
            21        layout region         -> ctx+0x11E  (0x8B/0x8C)
            00        currency symbol       -> ctx+0x11F  (0x89/0x8A)
            A7        behavior flags        -> ctx+0x120  (0x83)
            00 00 00  opaque bytes           -> ctx+0x121..0x123
            88 13     backlight timeout LE16 -> ctx+0x124  (0x85/0x86)
            58 02     idle interval LE16     -> ctx+0x126  (0x87/0x88)
```

- The three opaque bytes have no identified reader in V122; the loader
  brings them into RAM and the saver writes the full 12-byte RAM copy back,
  so stock writes preserve them. No USB getter exposes them; they cannot be
  assumed zero on the physical unit.
- **A USB success reply is command acceptance, not durability.** The reply
  is sent by the dispatcher return; the flash write happens afterwards in
  the main loop. Persistence must be verified by reconnect/power-cycle
  readback, not by the ACK.
- Page-write risk: the save erases the full 256-byte page before
  programming. An interruption during erase/program can invalidate the
  record (the `0x0C` marker check then restores defaults) and, as with any
  page operation, the window is not provably harmless to the rest of the
  page. No interruption test is authorized on the only keyboard.

## 5. ClicksFT cross-check and discrepancies

The ClicksFT table (pinned above) matches the V122 handlers on opcodes,
argument counts, mask semantics, and the currency/layout enumerations.
Confirmed discrepancies — **the V122 executable remains the authority**:

1. ClicksFT's expected replies for 0x8A and 0x8C omit the payload byte; V122
   replies `[05, 02, cmd, 00, value]` (`0x3706`, `0x375C`).
2. ClicksFT's "Turn CursorMode on" expected reply is `… 0x40`; V122 always
   echoes the complete final flags byte (`0x3586`), which would be `0xE7`
   from the factory state, not `0x40`.
3. Several ClicksFT examples assume unrelated flag bits are zero; the
   firmware preserves masked-out bits, so reply bytes on a live device
   depend on its full current state.

## 6. Client restrictions for any future settings tool

1. Read the complete current flags with the zero-mask query before any
   masked update; never hardcode expected flag bytes.
2. Restrict currency writes to `0..4` and layout writes to
   `0x20`/`0x21`; the firmware does not validate them.
3. Enforce client-side bounds the firmware does enforce (timeout >= 1000 ms;
   idle interval >= timeout in seconds) to avoid wasted round-trips, and
   treat status `0x01` as authoritative rejection.
4. Write only on real value change (the firmware already skips no-op
   stores; match that behavior client-side to minimize flash wear).
5. Never expose `0xF700` as an address; all access goes through the
   validated commands, which preserve unrelated fields.
6. No automatic or high-frequency profile switching: every changed record
   costs a page erase/program cycle.
7. One configuration owner at a time; concurrent writers can interleave
   dirty-flag saves.

## 7. Open items

- The exact user gesture that triggers the currency-glyph emission path
  (`0x2F7C–0x300A`, reached from the matrix row-5 bit-2 block at
  `0x2F32–0x2F42` with a state-byte condition on `ctx+0x0` bit 4) is not
  fully traced to its physical crossing; see the
  [capability ledger](stock-capability-ledger.md) open items.
- The three opaque record bytes (`ctx+0x121..0x123`) remain unidentified;
  no getter exposes them.
- The runtime status bits returned by 0x8D are identified only as state
  word bits 4 and 5; their meaning (what events set them) is not decoded.
- Nothing in this document authorizes executing any frame beyond the
  already-accepted getter baseline. The zero-mask `04 83 00 00` query
  requires a separate, explicit authorization decision.
