# CK-5200 stock-extension feasibility: interception and storage

Status: **OFFLINE RESEARCH. No patched image is authorized for installation;
the R3 recovery gate is unchanged and separate.** First increment of the
"Programmable Stock Extension Feasibility" milestone (Rudi's 2026-10-21
direction), covering interception (milestone deliverable 1). Compiled from
the V122 disassembly (image SHA-256
`8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`, base
`0x2000`).

The companion baseline documents are the
[settings protocol](stock-settings-protocol.md) and the
[capability ledger](stock-capability-ledger.md): unmodified V122 cannot do
arbitrary remapping; the open question is whether a stock-derived extension
can, while preserving OEM behavior.

## 1. The key-processing pipeline (verified)

```text
main init 0x2358 --jal--> scanner init 0x2A22
  0x2A22: obj->ctx = a1 (0x2A26); timeouts scaled from ctx fields (0x2A34 via 0x2934)
  0x2A36-0x2A42: builds callback pointers
      a3 = 0x2958 (layer/backlight handler): lui a3,0x3 ; addi a3,a3,-0x6a8
      a2 = 0x2796 (HID report builder):    lui a2,0x2 ; addi a2,a2,0x796
  0x2A4C: jal 0x25BC (registration): stores a2/a3 into a RAM config table
      (sw a2,0x8(sp) / sw a3,0xc(sp) at 0x2668/0x2666) later copied into the
      scanner object
scan tick 0x2A1E --jal--> scanner 0x24AA-0x26D6 --> invokes the stored
      report-builder callback per matrix change
report builder 0x2796 (ABI: a0 = state object, a1 = 6-byte matrix rows,
      a2 = capacity/size; returns a report)
  0x27DA: c.jal 0x2A54 (sticky-modifier latch processor)
  0x2796+0x2B1C..0x3010: per-crossing usage emission (see the ledger)
```

Caller census (complete):

| Function | Callers |
| --- | --- |
| `0x2A22` scanner init | exactly one: `0x2358` (`6ca000ef`, 32-bit jal) |
| `0x2A54` sticky-modifier latch (200 bytes, `0x2A54–0x2B1C`) | exactly one: `0x27DA` (`2cad`, 16-bit c.jal) |
| `0x2796` report builder | called through the RAM-stored callback pointer only |
| `0x2958` layer/backlight handler | called through the RAM-stored callback pointer only |

`0x2A54`'s ABI (from the decoded body): arguments `a0` = state object,
`a1` = matrix rows `[0..5]`, `a2` = report modifier byte, `a3` = 0xF
capacity; returns status in `a0`; clobbers only caller-saved registers
(`a0, a3–a7, t1`); the sole caller consumes only `a0` (`0x27DC`).

## 2. The range constraint (confirmed)

The `c.jal` at `0x27DA` (`2cad`) has a ±2 KiB displacement and cannot reach
an extension placed at `0x6900` (distance `0x4126`). Directly retargeting it
is not an option. A 32-bit J-type instruction at the callee entry could reach,
but at `0x2A54` it would consume the compressed load at `0x2A54` **and** the
32-bit load at `0x2A56`, so any callee-entry replacement must replicate both
instructions and re-enter at `0x2A5A`.

## 3. The minimal full-pipeline hook (recommended design)

The two instructions that **construct** the report-builder pointer are
self-contained argument builders with no other consumers:

| Address | Original | Encoding | Meaning |
| --- | --- | --- | --- |
| `0x2A3A` | `00002637` | `lui a2, 0x2` | a2 = 0x2000 |
| `0x2A42` | `79660613` | `addi a2, a2, 0x796` | a2 = 0x2796 (OEM builder) |

Replacing them with `lui a2, 0x7` (`00007637`) and
`addi a2, a2, -0x700` (`90060613`, immediate field `0x900` = two's
complement of `-0x700`) makes the registration store the **extension address
`0x6900`** (`0x7000 - 0x700`) as the report-builder callback. Encoding note:
the ADDI immediate is signed 12-bit (`-2048..2047`), so `+0x900` is not
representable; the decomposition must subtract from the LUI page. This
constraint is asserted by the patcher test. Effects:

- The entire key→HID pipeline is redirected: every scan change invokes the
  extension builder instead of `0x2796`, with the stock ABI.
- **No control flow, prologue, stack, or return semantics are touched.** The
  registration call at `0x2A4C`, the GPIO configuration in `0x25BC`, and
  everything else run identically; only one pointer value differs.
- **OEM behavior remains callable**: the extension builder can far-call the
  original `0x2796` (32-bit `jal` reaches: `0x6900→0x2796` = −`0x416A`),
  let it produce the stock report, then apply the custom keymap deltas —
  satisfying the milestone requirement that stock-default behavior is
  preserved while remapped keys emit configured events. With the custom
  keymap equal to defaults, output is bit-identical to stock.
- The layer/backlight handler `0x2958` (built at `0x2A36/0x2A3E`) can stay
  OEM or be redirected the same way if activation reconfiguration needs it.
- **Reversibility**: restoring the two recorded original words
  (`00002637`, `79660613`) restores the image bit-for-bit (verified by the
  patcher test below; reversal is per-site word-gated, and forward patching
  is accepted only from the hash-pinned V122 artifact).

Patch geometry: 8 bytes at file offsets `0xA3A` and `0xA42` (linked −`0x2000`),
both 32-bit-aligned, both inside the one-time registration path.

### Alternative hooks (recorded, not recommended)

- **Callee-entry at `0x2A54`**: single caller ✓, but reaches only the
  sticky-modifier stage and requires replicating a 6-byte prologue
  (`4138` + `0025C803`) with re-entry at `0x2A5A`. Kept as the interception
  point only if modifier-latch semantics alone need replacing.
- **Scanner-init entry at `0x2A22`**: single caller ✓ (`0x2358`). A 32-bit
  `j 0x6900` patch (original `f0eff2ef` recorded) with a trampoline that
  replays the WCH prologue (`jal t0, 0x2130`) then jumps to `0x2A26` would let
  the extension install pointers at runtime. More moving parts (stack-frame
  discipline inside the trampoline) with no advantage over the registration
  patch.

## 4. Storage geometry (milestone deliverable 3, first cut)

- Unused application capacity: `[0x68D8, 0x8A00)` = `0x2128` = **8,488 bytes**
  beyond the V122 image, before the staging area. The extension (builder
  replacement + engine) must fit together with any appended keymap records
  the image itself carries.
- A three-layer, 36-position keymap record is 36 × 3 × 2 = **216 bytes**
  plus headers; two profiles ≈ 0.5 KiB. The engine (deliverable 2) must be
  measured after compilation; the QMK-port comparison (deliverable 4) will
  size the alternative.
- **OEM update survival is NOT claimed**: an official update rewrites the
  application area and would remove the patch and extension; the boot stage's
  copy semantics remain unknown. Persistent keymaps that must survive OEM
  updates belong outside the application image — the only candidate with
  evidence is the settings page `[0xF700,0xF800)` (244 unused bytes after the
  12-byte settings record, preserved by OEM update image copies by
  construction, since no official image reaches that page); using it requires
  its own erase/wear and safety analysis before any design is claimed.
- Keymap writes on-device would use the stock RMW helper semantics (256-byte
  page granularity, erase-then-program) and inherit its interruption window.

## 5. Deliverable 2: the functional engine (v2, implemented and tested)

`stock_extension/ck5200_keymap_engine.c/.h` — host-testable, pure C99.
The 2026-10-22 correctness review required a v2; both its P1 model
findings are fixed at instruction level of the model:

- **Per-layer actions** (P1 fix): usage, modifiers, and activation are
  independently configurable on each of the three layers. A press
  resolves to ONE immutable action snapshot held for the life of the
  press, so a NAV Undo (GUI+Z) can never alter base-layer M — asserted by
  dedicated no-regression tests.
- **Elapsed-millisecond timing** (P1 fix): the caller supplies `now_ms`;
  tap windows and one-shot expiry are absolute deadlines, never callback
  counts. `ck_engine_next_deadline()` exposes the next time-driven
  update so the shim can schedule a tick with NO matrix change — the
  suite proves a tap's release frame arrives without any further key
  event.
- **Overlapping activators**: a press-order stack; releasing one
  activator falls back to the next still held (tested in both orders).
- **Base transparency**: an empty action on a layer inherits the
  base-layer action (matches the ledger's "-" columns) and lets
  base-defined activators — e.g. a toggle — still work while their layer
  is active.
- **Input boundary**: rows are masked to the six column bits; bits
  `0x40/0x80` cannot index out of bounds (tested).
- **Validation**: `ck_keymap_validate()` flags out-of-page usages and bad
  layer targets; the runtime structurally cannot emit a corrupted byte.

**Corrected stock baseline** (P1 fix): position 2/03 (row 2, mask 0x08)
is **Left Shift**, not a spare key — V122 `0x2AE6` (`andi a0, a6, 0x8` on
row 2, latch-exclusivity mask `0x17` at `0x2AF6`), the factory layout,
and the QMK decode all agree. The capability ledger carries a dated
correction; the shared fixture (`tests/ck5200_default_keymap.h`, one
fixture for both test binaries so corrections cannot diverge) models it
as the Shift modifier on every layer.

Suites (both hosted in firmware CI): the engine suite
(`tests/ck5200_keymap_engine_test.c`) runs **258 checks** including the
full 108-combination layer-position sweep, release-across-layer-switch
correctness in both directions, all four activation modes with
millisecond timing, modifier chords, and partial releases. The shim and
serialization suite
(`tests/ck5200_extension_shim_test.c`) runs 21 checks.

**Builder ABI** (unchanged, proven): `(a0 = key-state object,
a1 = matrix[6], a2 = 6)`; builder pointer at `scan_obj+0x1C`, layer
handler at `+0x18`.

## 6. Deliverable 3: measured storage design (v2 measurements)

All numbers below are measured, not estimated:

| Component | Size |
| --- | --- |
| Engine `.text` (pinned RISC-V toolchain, `-Os`) | 772 bytes (CI budget ≤ 1024) |
| **Serialized default keymap record** (packed format v1) | **350 bytes** |
| Complete linked extension (engine + shim + serializer + freestanding + embedded record) | **2,624 bytes** at `0x6900` |
| Complete research image (patched V122 + extension) | **21,272 bytes**, extent `0x7318` |
| Application-area capacity `[0x2000,0x8A00)` | 27,136 bytes — **headroom 5,864** |

The v1 figure of 216 bytes per record counted only usages (review
finding P2). The designed packed format (magic, version, entry count,
then one entry per non-empty action: position, layer, flags, usage or
activation+target, conditional mods/tap fields) measures 350 bytes for
the full default transcription and shrinks proportionally for delta
profiles. Deserialization is fail-closed: truncated, bad-magic,
out-of-page-usage, bad-target, and trailing-byte records are all
rejected without partial loading (tested).

`stock_extension/build_extension_image.sh` reproduces the linked image
end to end: hash-gated patch, generated record, target compilation,
link with the research linker script (`extension.ld`, `oem_send`
provided at the proven `0x278C`), binary extraction, and a capacity
assertion.

## 7. Deliverable 4 plus: the callback shim and linked image

The shim (`stock_extension/ck5200_extension_shim.c`) is the builder the
patched registration points at. It **replaces** the OEM builder for the
keyboard-page report — it never calls the OEM builder, so an altered key
replaces the OEM action rather than emitting both (asserted: exactly one
frame per tick). With the default keymap it emits stock-equivalent
frames through the engine; with remaps it emits the configured output.
The host harness proves: default frames (`type 020B`, mods, usage),
replacement semantics, and the time-driven tap release through
`ck_shim_next_deadline`.

**Pinned open items before any executable claim on the real path**
(deliberately not guessed in code, per the review):

1. The exact OEM report framing consumed by the send routine at
   `0x278C`: field `[2]` and the length formula at `0x2856–0x285E` are
   not fully decoded; the shim emits the decoded core with `[2]` zeroed.
2. The RAM address of the OEM 1 ms tick counter: the device build uses a
   per-call placeholder (`CK_TIME_UNPINNED`); the host harness supplies
   injected time and is the timing-verification path.
3. Real-image data placement: the research linker keeps `.data`/`.bss`
   after the flash sections for measurement only; a real image needs the
   engine state placed in RAM (and initialized) — via RAM sections with
   a startup hook or by relocating the engine state into an OEM-writable
   RAM location.
4. The decisive offline verification: execute the patched V122 pipeline
   in the chgame-based bounded assurance model (a NEW V122 profile; the
   existing gate is hash-pinned to the QMK binary and must not be
   modified) and compare complete HID reports against the engine
   across scripted scans, including stock-default byte-equivalence.

## 8. Deliverable 4: evidence-based comparison with QMK

| Axis | Stock-derived extension (this milestone) | QMK port (existing, PR #1–#3) |
| --- | --- | --- |
| Behavioral evidence | 36-check engine suite: stock-default equivalence, NAV remap goal (Esc/Tab/Undo/Redo/arrows), all four activation modes, release correctness | Full QMK feature set behind a verified 144-position RAM-keymap editor and CI-tested firmware; runtime liveness proven only in software models |
| Image footprint | ≈ 20.1 KiB total (V122 + ≤1.6 KiB extension) | 26,940 bytes verified |
| OEM stack | Preserved byte-for-byte: USB, iAP2, MFi authentication, OEM updater protocol all untouched | Replaced: TinyUSB port, re-engineered iAP2/auth path, own descriptors |
| Integration risk | Concentrated and small: 8-byte registration patch (reversible, tested), one callback ABI bridge, placement vs. future official images (a larger official image than `0x6900` bytes would overlap the extension region) | Spread across the whole application: every stack is new code on the device, though heavily reviewed and CI-verified |
| Stock restore | Official Clicks update overwrites the extension (de-facto restore, unverified) — same staging path as the M4 trial | Same M4 staging path (V122 restage), same unknown boot-stage caveats |
| Recovery dependency | **Identical for both: R3 unmet. Neither image may be installed until independent recovery is authorized.** | Same |
| Capability ceiling | Three layers × 36 positions, four activation modes, modifier chords | The full QMK engine (more layers, tap-hold timing rules, combos, macros) |

Assessment: the two routes are complementary, not competing. The
stock-derived extension is the minimal-risk path to the original goal
(Esc/Tab/Undo/Redo and cursor keys on configurable positions with the OEM
behavior and authentication intact); QMK remains the functional reference
and the only route to capabilities beyond the small engine's ceiling.
Per the milestone contract: if the stock-derived route cannot fit or
cannot preserve OEM behavior reliably, switch to QMK with the failure
documented. The measurements above show it fits with wide margin; the
remaining stock-route risk is behavioral (the callback shim and the
report-send path must be written and tested against the OEM send routine,
not merely sized).

## 9. Open items (updated 2026-10-22, v2 increment)

1. ~~Builder-callback invocation ABI~~ **DONE** (`0x2536–0x2542`,
   `0x259E–0x25A6`): `(a0 = key-state object, a1 = matrix[6], a2 = 6)`;
   builder at `scan_obj+0x1C`, layer handler at `+0x18`.
2. ~~Engine implementation and measurement~~ **DONE v2**: per-layer
   actions, immutable press snapshots, activator stack, millisecond
   timing with deadlines, base transparency, boundary masking; 258-check
   engine suite plus the 21-check shim/serialization suite; 772-byte
   target `.text` (§5).
3. ~~Serialized storage design~~ **DONE**: packed format v1 measured at
   350 bytes per default record, fail-closed deserialization, linked
   extension 2,624 bytes, complete image 21,272 bytes with 5,864 bytes
   headroom (§6).
4. Appended-code placement rules: a future official image larger than
   `0x6900` bytes overlaps the extension region and invalidates the
   layout — check against the manifest sizes at each update.
5. The OEM report framing consumed by `0x278C` (field `[2]`, length
   formula at `0x2856–0x285E`) and the OEM 1 ms tick RAM address: pinned
   open items in the shim (§7); both must be decoded before any
   executable claim on the real send path.
6. Real-image RAM placement and initialization for the extension state
   (the research linker keeps `.data`/`.bss` after flash sections for
   measurement only).
7. The decisive offline verification: a NEW V122 execution profile on the
   chgame bounded assurance model (the existing gate is hash-pinned to
   the QMK binary and must not be modified) driving the patched pipeline
   end to end and comparing complete HID reports against the engine —
   including stock-default byte-equivalence and remapped keys replacing,
   not duplicating, OEM output. The engine and shim suites verify the
   model logic; this step verifies the actual V122 execution path.
4. Behavioral equivalence harness: stock-default keymap → byte-identical
   reports vs. the unpatched OEM builder, run offline against scripted scans.

## 10. Demonstrated offline patcher (research artifact)

`keyboards/ck5200/tools/stock_extension_patch.py` applies or reverses the
8-byte registration redirect against the hash-verified V122 image and emits
a manifest (offsets, original/replacement encodings, hashes). Its test
(`keyboards/ck5200/tests/test_stock_extension_patch.py`) verifies:

- patch applies only to the exact V122 image (hash-gated),
- the two words at `0x2A3A/0x2A42` decode to the intended `lui/addi` pair
  building `0x6900`,
- reversal reproduces the original image hash bit-for-bit,
- any other image is refused.

**A patched image is a research artifact. Nothing in this document
authorizes writing it to the keyboard; installation remains gated by the
unchanged R3 recovery decision.**
