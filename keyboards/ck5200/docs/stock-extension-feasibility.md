# CK-5200 stock-extension feasibility: interception and storage

Status: **The extension installer is enabled only in the explicit
ExtensionInstaller app configuration, at Rudi's 2026-10-23 request.
No agent-executed keyboard flash is authorized or performed. R3 remains unmet.** First increment of the
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

**Current evidence: section 19.** Rudi accepted `9113dab` only as an
offline correctness-and-build milestone. The OEM executable integration
milestone remains open. Earlier measurements below describe that build.

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
   a2 = capacity/size; submits reports)
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
   preserving the option of OEM delegation. Current default equivalence
   comes from delegation, not from the engine reproducing all OEM rules.
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
- **[CORRECTION]** The 216-byte estimate counted usages only. It is not a
  configuration storage budget. Format v2 allows records up to 978 bytes
  (section 11).
- **OEM update survival is NOT claimed**: an official update rewrites the
  application area and would remove the patch and extension; the boot stage's
  copy semantics remain unknown. Persistent keymaps that must survive OEM
  updates belong outside the application image — the only candidate with
  evidence is the settings page `[0xF700,0xF800)` (244 unused bytes after the
   12-byte settings record). No update-survival guarantee follows because
   the boot-stage copy rules remain unknown. Using this page requires
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
- **Explicit transparency (review correction)**: only `TRANSPARENT`
  inherits Base. `NO` disables a key. Empty actions no longer imply
  transparency. The shared fixture marks inherited positions explicitly.
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
a1 = matrix[6], a2 = 6)`; builder pointer at `scan_obj+0x18`, layer
handler at `+0x1C`. **[CORRECTION]** The earlier record reversed these
offsets. Registration stores them at `0x269E/0x26A0`. Invocation of the
builder occurs at `0x25A6`.

## 6. Deliverable 3: measured storage design (v2 measurements)

Historical measurements at `9113dab`, superseded by section 11:

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
rejected by the active caller. **[CORRECTION]** At `9113dab`, rejection
could alter the destination. The new decoder validates before mutation.

`stock_extension/build_extension_image.sh` reproduces the linked image
end to end: hash-gated patch, generated record, target compilation,
link with the research linker script (`extension.ld`, `oem_send`
provided at the proven `0x278C`), binary extraction, and a capacity
assertion.

## 7. Deliverable 4 plus: the callback shim and linked image

**[CORRECTION]** The `9113dab` shim did not prove stock equivalence.
Its frame omitted the reserved keyboard byte, its entry placement was
not guaranteed, its state occupied flash, and it omitted EP1 submission.
Host tests of that frame did not verify OEM execution.

At `57fef66`, disabled mode delegated to the original builder and enabled
mode replaced its keyboard output. Section 12 adds a native-policy bridge
for enabled mode. Full IRQ and transport compatibility remains unproven.

## 8. Deliverable 4: evidence-based comparison with QMK

| Axis | Stock-derived extension (this milestone) | QMK port (existing, PR #1–#3) |
| --- | --- | --- |
| Behavioral evidence | Host suites plus a bounded V122 scanner profile. Default delegation matches 74 complete submissions. Enabled NAV and idle tap releases execute through OEM EP1 submission. Full OEM behavior in enabled mode is not established. | Full QMK feature set behind a verified 144-position RAM-keymap editor and CI-tested firmware; runtime liveness proven only in software models |
| Image footprint | 27,058 bytes, current research build | 26,940 bytes verified |
| OEM stack | OEM code retained, with registration, startup-call, and timer-call hooks. Runtime compatibility still requires verification. | Replaced: TinyUSB port, re-engineered iAP2/auth path, own descriptors |
| Integration risk | Eight patched instruction words, extension SRAM, timer scheduling, native-state preview, two transports, and OEM behavior preservation | Spread across the whole application: every stack is new code on the device, though heavily reviewed and CI-verified |
| Stock restore | Official Clicks update overwrites the extension (de-facto restore, unverified) — same staging path as the M4 trial | Same M4 staging path (V122 restage), same unknown boot-stage caveats |
| Recovery dependency | **Identical for both: R3 unmet. Neither image may be installed until independent recovery is authorized.** | Same |
| Capability ceiling | Three layers × 36 positions, four activation modes, modifier chords | The full QMK engine (more layers, tap-hold timing rules, combos, macros) |

Assessment: the two routes are complementary, not competing. The
stock-derived extension is the current preferred research route to the goal
(Esc/Tab/Undo/Redo and cursor keys on configurable positions with the OEM
behavior and authentication intact); QMK remains the functional reference
and the only route to capabilities beyond the small engine's ceiling.
Per the milestone contract: if the stock-derived route cannot fit or
cannot preserve OEM behavior reliably, switch to QMK with the failure
documented. The current image fits the application area. This does not
establish lower installation risk or full OEM compatibility.

## 9. Remaining integration gates

1. Extend section 14's write and interrupt audits beyond their scripted
   states, including NMI/fault paths and timer coalescing. Current evidence
   establishes bounded preview isolation, not all-path hardware reliability.
2. Extend execution beyond the synthetic handoff: full SystemInit/main
   execution, actual interrupt entry/return, USB enumeration and completion
   interrupts, suspend/reconnect, and companion transport backpressure.
   Current EP1 readiness is a modeled input, not a controller interrupt proof.
3. Verify FIFO overflow recovery and delayed/coalesced timer interrupts.
   The implemented overflow policy drains accepted reports, emits all-up,
   and mutes remaps until all physical keys are released. It is not lossless.
4. Section 14 implements an offline capability handshake and atomic RAM
   record operations. Connect no physical editor until lifecycle acceptance
   and authorization permit it. Keyboard persistence does not fit the
   present application budget. No other storage region is approved.
5. Recheck placement for each OEM version. An image ending after `0x6900`
   overlaps this extension. R3 remains unmet for both the extension and QMK.

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

## 11. Accepted bounded-execution baseline, `57fef66`

Rudi accepted this bounded proof. Firmware CI `39792638882` and iOS CI
`39792638881` both passed. The details below describe `57fef66`, not the
new active-policy bridge. Full OEM executable integration remains open.

### Placement and initialization

- `KEEP(*(.text.ck_extension_builder))` places the callback first.
  `ASSERT(ck_extension_builder == 0x6900)` enforces its address. The build
  also checks the symbol table and disassembly.
- V122 startup sets GP to `0x20000840`, SP to `0x20005000`, copies
  `[0x6880,0x68D8)` into `[0x20000000,0x20000058)`, and clears BSS through
  `0x200006B4` (`0x485C–0x48A6`).
- The linker reserves `[0x200006C0,0x20002000)` for extension SRAM.
  Current BSS occupies `[0x200006C0,0x20000F79)`, 2,233 bytes. The upper
  12 KiB remains available for the stack. The bounded profile observes
  minimum SP `0x20004864`. This is not an all-path stack bound.
- A new JAL hook at `0x48CE` runs `ck_extension_init`, then calls the
  original SystemInit at `0x3BBA`. The profile seeds RAM with `0xA5` and
  executes the genuine startup path to that call. It checks OEM data,
  OEM BSS, and every extension BSS byte. Full SystemInit is not executed.
- The complete image now patches four instruction words, 16 bytes:
  `0x2A3A`, `0x2A42`, `0x48CE`, and `0x3AEE`. Startup/timer targets come
  from ELF symbols. Per-site reversal reproduces the exact V122 hash.
  The two-word registration-only patch remains separately usable offline.

### Report and timer boundaries

- `0x27A8–0x27B8` zeroes the packet and sets `[0B,02]`. Byte `[2]` is
  zero. The eight-byte boot keyboard report begins at `[3]`: modifier,
  reserved, then six usages. The previous shim misplaced usages at `[4]`.
- `0x2856–0x2860` computes length as `frame[0] + frame[11]`.
  The keyboard-only packet is 11 bytes. The OEM path can append a
  four-byte consumer record, making 15 bytes.
- `0x2864` calls companion submission `0x278C`, through `0x4788` and
  session-gated `0x62CC`. `0x286C` separately calls `0x3CBE(report,8)`,
  which submits EP1 through `0x3E28`. These are not interchangeable APIs.
- `0x3E28` copies immediately into the EP1 DMA buffer and sets ACK. It
  has no busy check. The extension FIFO waits for TX response NAK at
  `0x40023426` before submitting. OEM IN completion sets NAK at
  `0x40CA–0x40D6`. FIFO behavior is tested against a modeled ready host
  and a host consuming only every 17 nominal milliseconds.
- The real SysTick handler is `0x3AE6`, not the stub at `0x4810`.
  `0x3AEE` calls the timer driver `0x22DA`, which reaches scanner tick
  `0x29F4` and scanner `0x24AA`. V122 configures the nominal 1 ms timer
  at `0x3B9E–0x3BB2`. No free-running OEM millisecond RAM global is claimed.
- The new hook increments a counter per SysTick service, calls the OEM
  timer driver, then handles due deadlines and queued reports even with
  an idle scanner. It no longer counts builder callbacks. Delayed or
  coalesced interrupts can affect this clock and remain an open test.

### State and storage corrections

- Held activators resolve their target from the immutable press snapshot.
  Toggle and one-shot targets use independent snapshots after release.
  Same-scan activators resolve in row/column order. TAP interruption is
  per press, so a later activator cannot clear an earlier interruption.
- `NO` and `TRANSPARENT` are independent actions on all three layers.
  Base transparency emits nothing. The shared fixture explicitly marks
  inherited positions instead of inferring transparency from empty actions.
- Format v2 rejects format v1 rather than reinterpreting old records.
  A validation pass precedes destination mutation. Duplicate entries,
  unknown flags, malformed actions, truncation, and trailing data fail.
- The embedded fixture now occupies 461 bytes. The worst allowed record
  occupies **978 bytes**: six header bytes plus 108 nine-byte entries.
  The generator uses this bound. A maximum-size round trip and a short
  buffer rejection are tested. This is not persistent storage.
- The expanded engine measures 1,126 bytes under the existing footprint
  test flags. Its sub-budget changes from 1,024 to 1,280 bytes. The complete
  image remains subject to the unchanged 27,136-byte application limit.

### Reproduction and exact limits

```sh
cd keyboards/ck5200
bash stock_extension/build_extension_image.sh
python3 stock_extension/assurance/run.py
```

The V122 profile uses the pinned CHGame CPU and existing guard patch in
its own build directory. It does not change `assurance/run.py`, its
QMK hashes, its model, or any external checkout. Unknown memory accesses,
flash writes, wrong callback targets, stack overlap, and EP1 overwrite
before completion fail the profile.

Five cases retain ordered JSONL evidence:

1. Stock V122: Q plus a press/release at each of the 36 positions.
2. Extended V122, remapping disabled: the same 74 companion submissions,
   74 complete HID submissions, and 74 consumed EP1 packets, byte-for-byte
   and at the same scripted times. This proves **delegation**, not engine
   equivalence to the complete OEM behavior.
3. Remapping enabled through a synthetic RAM fixture: exact ordered Q,
   Esc, Tab, GUI+Z, GUI+Shift+Z, Up, and idle TAP press/release reports.
   No USB configuration command is fabricated or claimed.
4. The same remaps with EP1 host consumption every 17 nominal milliseconds.
5. Negative control: removing only the timer hook causes the final tap
   release to disappear. The profile requires detection of this failure.

Execution includes genuine CRT instructions, the real scanner initializer,
GPIO scan instructions, the registered callback, both OEM submission
functions, and the EP1 DMA copy. It stops CRT before SystemInit. It invokes
the real timer driver's JAL target, not interrupt entry/return. Settings,
GPIO closures, and EP1 completion are synthetic. Enumeration, iAP2/MFi
delivery, complete boot, and physical timing remain unproven.

Current image: **22,441 bytes**, extent `0x77A9`, with **4,695 bytes**
headroom. This includes the unchanged-size V122 base, a 40-byte alignment
gap, and 3,753 bytes beginning at `0x6900`. BIN SHA-256:
`8165f272701baaf20d8da7e8544a5bf89d2c4aa352de8b5f9a97ae15abecf7b0`.

Firmware CI retains `ck5200-stock-extension-research`: ELF, BIN, map,
disassembly, symbols, SHA256SUMS, patch manifest, execution log, report,
and all traces. The report binds the executed images and ELF to hashes.
The profile verifies ELF/BIN identity before execution.

No keyboard command, firmware write, reset, ISP operation, or vendor
contact occurred. R3, M4, and the separate physical authorization gates
remain unchanged.

## 12. Active native-policy preservation after the `57fef66` review

This section records the accepted baseline. Firmware CI `39804442651`
and iOS CI `39804442643` passed at `21a985c`. Section 13 describes the
subsequent native/custom layer increment.

Status: **active-policy comparisons pass in the bounded model. Full OEM
integration remains OPEN.** No physical command or settings write ran.
The QMK gate, iOS source, M4 trial, and vendor inquiry remain untouched.

### Corrections and implementation

- The new regression reproduced the shared `tap_fire_pos` defect. Two
  qualifying releases within the same millisecond left the first usage
  active. Each held snapshot now owns its pending release and deadline.
  Both releases clear without another matrix event. Repressing the same
  position also clears correctly.
- **[CORRECTION]** The V122 descriptor at `0x6580`, 97 bytes, defines four
  key slots, four left modifier bits, and three consumer bits. It is not
  a six-slot boot report. The eight input bytes contain: modifier bits
  and padding, one reserved byte, four usages, a consumer bitmap, and
  padding. The profile parses the descriptor and asserts all input fields.
  Custom output uses four slots and cannot overwrite the consumer byte.
  More than four usages produces ErrorRollOver in those four slots only.
  Unsupported right-modifier bits now fail keymap validation.
- `OEM` is an explicit action, separate from KEY, NO, and TRANSPARENT.
  The device default uses OEM actions on all 108 entries. The engine
  snapshots native ownership at press time. Custom activators and disabled
  keys do not reach the native builder or its layer/backlight handler.
- The original V122 code still implements native consumer crossings,
  sticky modifiers, currency, layouts, and Clicks gestures. The bridge
  captures complete native frames, adds custom keyboard output, and
  preserves consumer fields and optional payloads. It retains native
  repeated submissions rather than deduplicating their events.
- A mixed regression exposed another defect: simply masking a remapped
  letter prevented it from applying and consuming sticky Shift. The
  bridge now supplies ordinary-key activity to the original latch policy.
  The marker's usage must not appear in output. A preview executes the
  original builder against copies of its 0x44-byte state and 0x128-byte
  context to obtain native key slots without the marker. The real call
  updates native state exactly once. Its modifier/consumer fields and
  special packets remain authoritative. No second implementation of OEM
  gesture rules was added.
- Preview transport calls are suppressed. Real special packets retain
  their original transport path. **Nested IRQ safety and companion
  backpressure are not proven.** The capture flags are shared state and
  require interrupt-path verification before any executable-ready claim.
- The image patches eight words, 32 bytes: builder registration at
  `0x2A3A/0x2A42`, layer registration at `0x2A36/0x2A3E`, startup at
  `0x48CE`, timer at `0x3AEE`, companion submission at `0x4788`, and HID
  submission at `0x3CBE`. The companion wrapper forwards to
  `0x62CC(0x20000460, frame, len)`, the same context used by original
  `0x4788`. The HID wrapper forwards to original `0x3E28` for EP1.
  The build derives targets from ELF symbols and reverses all sites back
  to the pinned stock hash. The accepted `57fef66` image remains in Git history.

### Bounded execution evidence

The existing profile now executes ten cases, including the original
missing-timer negative control:

| Comparison | Observed result |
| --- | --- |
| Stock vs disabled delegation | Same 74 complete HID submissions |
| Stock vs enabled engine with all-OEM actions | Same 74 complete HID submissions, engine invocation verified |
| Stock vs enabled custom NAV activator with unchanged native keys | Same 2,502 HID submissions and 2,542 complete companion packets |
| Sticky Shift around Q remapped to Esc | Same complete packets after substituting only Q's keyboard usage, including the following unmodified U |
| NAV Esc/Tab/Undo/Redo/Up, four-key plus consumer chord, idle TAP | Exact ordered 18 HID submissions |
| Same remaps with host readiness every 17 nominal ms | Same ordered 18 HID submissions |
| Missing timer hook | Final TAP release disappears, detected as required |

The native comparison spans 130 combinations: thirteen flag values, both
layout values (`0x20/0x21`), and all five currencies. It exercises sticky
Shift, held Symbols, layout-sensitive Z, the currency/Clicks path,
consumer chords, cursor entry, Space, and Enter. Instruction witnesses
confirm all five currency branches, both layout values, sticky processing,
and cursor entry. Comparisons include entire companion and HID packets,
not only usage bytes. Enabled cases must execute the remapping engine.
The active-NAV comparison checks event order and payloads, not identical
timestamps. Holding a custom activator changes the scanner's idle phase.

These remain synthetic settings and GPIO inputs. The model stops CRT
before SystemInit and invokes the real timer driver's JAL target without
IRQ entry/return. EP1 completion is still supplied by the model. There is
no enumeration, suspend/reconnect proof, open iAP2 session, iPhone editing,
persistent profile, or installation proof. Persistence remains a separate
gate after RAM editing and lifecycle verification.

### Current bounds

- Packed format v3 adds explicit OEM actions and rejects earlier formats.
  The native default is 330 bytes. The maximum remains 978 bytes.
  Rejected records leave their destination unchanged. No flash storage
  region is selected or authorized.
- The measured engine sub-budget changes from 1,280 to 1,536 bytes.
  Actual engine text under the footprint-test flags is 1,392 bytes.
- Complete image: 23,702 bytes, extent `0x7C96`, with 3,434 bytes of
  application headroom. Extension payload: 5,014 bytes after a 40-byte gap.
  BSS occupies `[0x200006C0,0x20000FBD)`, below the unchanged stack boundary.
- BIN SHA-256:
  `55cd6f544a1ecd8a6eb109e3bc585a9f391a3c137efa54bbd74124c2f09834b7`.

Run the same build and profile commands from section 11. Firmware CI
retains the ELF, BIN, map, disassembly, hashes, manifest, report, and traces
in `ck5200-stock-extension-research`. Hosted status must be checked for
the exact new commit. Local success does not imply hosted success.

## 13. Native/custom layer ownership after the `21a985c` review

Status at `d6d3461`: **accepted bounded native/custom layer ownership.**
Firmware CI `39821804798` and iOS CI `39821804796` passed at that exact
commit. The remaining details in this section describe that baseline.
Full OEM integration remains open. No physical operation is authorized.

### Activation contract

- The generated map enables native SYM and NAV synchronization. Remapping
  itself still starts disabled, with no device configuration channel.
- `native_mask` enables SYM and NAV independently. Disabling a native mode
  disables its native policy selection as well as its programmable fallback.
  Cursor activation remains available when native Symbols is disabled.
- Custom held activators take priority in press order. Then the engine
  uses an armed one-shot, then a toggle, then native NAV, then native SYM,
  then Base. Every custom mode can explicitly select Base, SYM, or NAV.
- A scan processes releases first, initial-layer activators second, and
  remaining presses last. Every phase uses stable row/column order. The
  initial activators keep their resolved targets even if an earlier
  activator changes the layer in that scan.
- Replacing the original Symbols or GUI action removes its native role.
  Unchanged gesture participants retain OEM ownership across native mode
  entry. Explicit custom activation can replace that ownership.
- Custom actions keep press-time snapshots. OEM actions selected under a
  custom layer also keep their output-layer projection until release.
  Separate native previews render simultaneous OEM keys from different
  custom layers. Underlying native tracking remains separate.

### Native state and bounded evidence

`ck_native_modes()` follows V122's Symbols selector at `0x2A62–0x2AE4`.
With sticky Symbols enabled and GUI absent, it reads key-state `+0x0C`
bit 1. Otherwise it reads physical row 5 bit `0x10`. Cursor selection
reads context status bit `0x10`, set by OEM instructions, not a guessed
held-key chord. The genuine gesture branch at `0x2B20–0x2B60` requires
flag `0x80`, exact row 2 `0x20`, exact row 5 `0x10`, and other rows empty.
The runner checks register `a5` at OEM PC `0x2A84` against these selector
inputs. Sticky-with-key-up and GUI-suppression witnesses must execute.

The eight new cases check 299 complete host HID states:

| Case | Required behavior |
| --- | --- |
| `layers-sync` | Held/sticky Symbols, GUI suppression, genuine Cursor entry, unused NAV position, custom Base priority, clean releases |
| `layers-sym-off` | Symbols disabled while native Cursor still selects NAV |
| `layers-nav-off` | Cursor disabled while native Symbols still selects SYM |
| `layers-replace` | Original activation keys replaced without duplicate native roles, coalesced conflicts, mixed OEM projections and release snapshots |
| `layers-modes` | All four custom modes target all three layers, native fallback, overlapping activators, snapshotted same-scan targets |
| Three layer sweeps | Each of 36 positions emits its assigned usage and releases on Base, SYM, and NAV |

OEM `0x2810–0x2846` exits Cursor on non-arrow activity. The sweeps enter
Cursor again before each NAV assignment. They do not change that OEM rule.
Native-policy copies preserve settings flags and layout. Tracking calls
commit only their owned 0x20-byte state prefix and context status byte.
This bounded evidence does not establish that every OEM path writes only
these fields. A complete side-effect and interrupt audit remains open.

The original ten cases still pass. The 130 selected settings combinations
still match 2,502 HID and 2,542 companion submissions with remapping active.
They no longer hold a custom NAV activator during native equivalence:
custom activation now intentionally changes OEM output. The remap chord's
golden report changes from `0000180e17290400` to `0000185117290400` because
custom NAV projects unchanged OEM K to Down. Consumer bits remain separate.
The missing-timer negative control still detects the lost tap release.

### Current bounds and open gates

- Format v4 stores count in metadata's low byte and `native_mask` in its
  high byte. The header stays six bytes. Reserved mask bits and old formats
  fail before destination changes. Default: 330 bytes. Maximum: 978 bytes.
- Engine text: 1,892 bytes under the footprint-test flags. Its sub-budget
  is now 1,920 bytes for mode ownership and phased transitions. The total
  application limit stays 27,136 bytes.
- Image: 25,466 bytes, extent `0x837A`, extension payload 6,778 bytes.
  Headroom: 1,670 bytes. BSS: `[0x200006C0,0x20000FCB)`.
- BIN SHA-256:
  `bb83d17bb13d1846e2548da271b430bb94ff4debcffaf8657c77e21bb8e89e7f`.

Use section 11's build and execution commands. The profile runs actual
V122 and linked extension instructions under address/undefined sanitizers.
GPIO, settings, RAM map replacement, and host completion remain synthetic.
No iPhone handshake, safe runtime editor, persistence journal, complete
startup, interrupt entry/return, nested capture safety, enumeration,
suspend/reconnect, companion backpressure, or overflow recovery is proven.
The remaining flash bytes are not a verified persistence allocation.

## 14. Offline RAM configuration and USB safety

Status at `1cbb07a`: **accepted offline RAM-configuration milestone.**
Firmware CI `39846660539` and iOS CI `39846660777` passed at that exact commit.
The remaining details in this section describe that baseline. This increment does not
authorize installation, physical configuration, M4 trials, or vendor contact.

### RAM configuration and iOS proof

The patched JAL at `0x2218` routes the original companion command adapter
through `ck_extension_command()`. Other opcodes retain the original
`0x3216` dispatcher. Replies bypass its small settings-adjacent response
buffer and use original companion transport and EP3 submission.

Protocol 1 uses command `0x94`. Multi-byte fields are little-endian:

- Request: `[length,94,1,op,nonce32,generation16,offset16,payload...]`.
- Reply: `[length,02,94,status,1,op,nonce32,generation16,payload...]`.
- Header: 12 bytes. Maximum frame: 36 bytes. Maximum chunk: 24 bytes.
- Status: 0 success, 1 invalid operation/record, 2 busy, 3 identity/session
  rejection, 4 stale or exhausted generation. Generation never wraps.

| Operation | Contract |
| --- | --- |
| 0 HELLO | Requires nonzero nonce. Returns `CKX1`, record format 4, dimensions 3/6/6, record bound 978, chunk bound 24, RAM-only feature bit, current record length, and remapping-enabled byte. Discards incomplete staging. Does not enable remapping. |
| 1 READ | `offset` selects a packed-record byte. One payload byte declares count 1..24. Generation must match. |
| 2 BEGIN | `offset` declares total record size 6..978. Clears staging progress without changing the active map. |
| 3 WRITE | Requires the exact next offset and at most 24 payload bytes. Rejects gaps, duplicates, and overflow. |
| 4 APPLY | Requires a complete valid record, matching generation, all keys released, no engine deadline, no report backlog/fault, and a ready configured, awake host. One payload byte explicitly enables or disables remapping. The interrupt-masked decoder commits the whole record and increments generation. |
| 5 ABORT | Discards staged bytes. Does not alter active mappings. |

The nonce binds replies to a client session. It is not an authentication
or cryptographic device-identity mechanism. Unmodified V122 cannot produce
the capability signature, dimensions, format, feature bits, and echoed
nonce. The Swift client validates all of them before any mutation.

`StockExtensionProtocol.swift` implements typed actions, all 108 positions,
strict format-v4 encoding/decoding, handshake, staged apply, readback, and
session invalidation on lost replies. It supports the unchanged keyboard
usage/modifier boundary. It does not add arbitrary consumer actions or macros.
The Codable map round-trips a full maximum-size profile for iPhone storage.
No live `AccessoryInspector` link or editor UI is connected to this client.

The Swift execution test supplies bytes to real V122 `0x2208`. It does not
write the keymap or enable byte directly. The actual command adapter,
extension, EP3 sender/completion ISR, native gestures, EP1 sender, and EP1
completion ISR execute. The test changes SYM Q to Esc and NAV 4/4 to Tab,
applies, reads back, and checks complete HID presses/releases. It also
applies a 978-byte map and recovers a committed map after a lost APPLY
acknowledgement. The client never retries that mutation automatically.

### Interrupts, preview isolation, and transports

- The IRQ cases retain CRT's actual `mtvec=0x2007` and `intsyscr=3`.
  PFIC requests enter original SysTick `0x3AE6` and USB `0x405A`, then
  execute original `mret`. Register and trap-depth checks verify returns.
  Higher-priority USB completion preempts SysTick outside capture.
- Builder, layer, and extension timer work preserve the caller's MIE bit
  and run atomically against reset and configuration interrupts. A USB request
  injected during preview remains pending until capture ends. No unrelated
  USB handler enters while the shared capture flags are active.
- Every tested preview write must target the live stack, the owned 20-byte
  RV32 preview frame, or capture flags. Any other RAM/MMIO/flash write fails.
  Real copied-policy writes are checked against the copied-back prefix,
  status byte, and backlight flag. These are execution-path checks, not
  an assertion that unexecuted OEM paths cannot have other effects.
- The audit exposed a real preview PWM write at `0x4F0E` to `0x40012C20`.
  A new hook at OEM PWM call `0x3062` suppresses previews only. Real native
  backlight gestures still execute both PWM register writes. Native-disable
  views also copy back the genuine backlight toggle at context `+0x120`
  bit 2 without persisting temporary mode masks. Two regressions check
  normal policy and disabled native NAV. The missing-PWM-hook negative
  control must reproduce the forbidden write.
- Projected OEM groups now merge consumer bits as well as keyboard slots
  and modifiers. Five checks compare entire HID and companion packets for
  concurrent Base/NAV consumer actions, release snapshots, and the real
  optional Cursor-exit payload. Notifications follow real native tracking,
  not state changes made only inside a preview.
- FIFO progress tracks HID and companion delivery separately. A blocked
  authenticated companion endpoint cannot hold a keyboard release hostage.
  Native four-byte Cursor notifications share the ordered companion queue.
  Overflow is not lossless: it mutes new remaps, retains accepted reports,
  and retries terminal all-up on HID and an authenticated companion before
  recovery. An absent companion retains OEM optional-link behavior.
- A reversible entry hook at `0x3D5E` invalidates configuration sessions
  and staging on bus reset, then executes original reset code from `0x3D62`
  with the displaced LUI restored. Reports wait for configured/awake USB.
  Real suspend/resume/reset ISR branches execute in the lifecycle test.
  Initial USB reset cannot strand remapping in the disabled state. Fault
  handling mutes new presses but still feeds real releases into native
  latch tracking, so old snapshots cannot appear held after reconnect.

The Python configuration trace covers 978-byte apply/readback, malformed
and incomplete records, held-key rejection, stale generation, interrupted
staging, reset invalidation, suspend/release/resume, companion backpressure,
queue saturation, notification retention, and final release on EP1 and EP3.
Its final all-up reports and a full eight-slot overflow are asserted.
One reset is requested during a preview and must remain deferred until the
builder mutation ends. The harness wires original reset/completion callbacks
explicitly. The reset callback executes OEM EP3 progress and iAP2-state cleanup.

**Limits:** USB configuration and authenticated companion state are still
synthetic handoffs. EP0 enumeration, actual iAP2 authentication, full
SystemInit/main, NMI/fault interleavings, and physical timing remain open.
The largest observed masked interval in the configuration test is 137,940
model cycles, about 2.87 ms at the model's 48 MHz. Real SysTick coalescing
and wall-clock deadline accuracy across that interval are not proven.
Passing these tests does not close full USB lifecycle acceptance.

### Persistence decision and memory budget

Decision: **RAM-only extension phase with iPhone-backed profiles. No
keyboard-flash persistence implementation or storage address is approved.**
This is an explicit interim limit, not fulfillment of keyboard persistence.

| Budget | Current measurement |
| --- | --- |
| Application limit | 27,136 bytes, unchanged |
| Image / extent | 27,058 bytes / `0x89B2` |
| Raw headroom | 78 bytes |
| Next free page / complete free pages | `0x8A00` / zero |
| BSS | `[0x200006C0,0x200013AB)` |
| Minimum observed SP | `0x20004864` |
| Serialized default / maximum | 330 / 978 bytes, unchanged |

The device now creates its identical all-OEM default map with a RAM loop.
The 330-byte serialized default remains generated as reference evidence,
but is not linked into flash. No mapping or record capacity was removed.

A lower-bound A/B estimate uses 978 record bytes plus 20 metadata bytes
(magic, format, length, generation, CRC, commit marker), padded to four
256-byte pages. Two independent slots need eight pages, 2,048 bytes, before
journal/flash code. This is not a validated journal layout. Commit granularity,
power-loss behavior, and wear handling need separate proof and can require
more space. No complete page remains in the current application.
The 244 unused bytes of OEM page `0xF700` cannot hold one record. Update
staging, presumed installer, and upper reserved regions are not available
storage allocations. Power-loss recovery and wear handling therefore have
no valid target here. Do not implement a journal by subtracting record
lengths from `0x8A00`.

Current BIN SHA-256:
`2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132`.
The patcher reverses all eleven words to exact pinned V122. The build adds
a source-hash manifest, and execution refuses firmware built from stale
sources. The exact-QMK hash gate is untouched.

Local checks: 37 Python tests, existing native and Swift suites, 20 V122
cases including two negative controls, and the Swift-to-V122 configuration
test. The unsigned diagnostics-only iPhone application also builds locally.
Firmware CI runs the Swift integration separately and retains its
log, traces, ELF/BIN, source manifest, and execution report. Hosted success
must refer to the exact new commit, not these local results.

## 15. Live editor software integration

Status at `cf99619`: **local software verification passed, but hosted editor verification failed before Simulator tests.**
In iOS run `39858956521`, the main `verify` job passed. The `extension-editor`
job failed with missing `build/stock_extension/symbols.txt`. The following
section describes that revision. Section 16 records the correction.
This increment does not change the extension image or authorize physical commands.
`1cbb07a` remains the accepted offline baseline. Its iOS CI is green, not pending.

### Client, streams, and ownership

`StockExtensionSession` retains the same codec and protocol-1 checks but now
awaits asynchronous transfers. A busy guard covers complete operations, including
every stage and the final handshake/readback. Concurrent callers receive `busy`.
There is no queued retry of an uncertain mutation.

`StockExtensionTransport` connects that client to `KeyboardPacketLink`, including
the existing EASession-backed `StreamLink`. It assembles frames with a 36-byte
bound, skips OEM key/status notifications, and accepts fragmented replies.
Timeout, cancellation, disconnection, or invalid framing closes the adapter and
discards pending output. Reconnect uses a fresh nonce and reads the committed map.
An OEM notification can start after a reply and finish during the next exchange.
Frame-start tracking prevents that earlier frame from becoming the next reply.

`AccessoryInspector` owns stream routing and owner changes. Stock recording,
stock read sweeps, QMK keymap operations, and firmware transactions cannot share
an active extension exchange. Owner changes close the previous link first.
The management guard checks actual in-flight operations, not only the phase label.
Backgrounding closes the editor link and invalidates pending work.

The physical extension entry point checks a false authorization gate before
creating an EASession. The controller also rejects physical editor links.
No iPhone build sends extension commands to an accessory in this increment.
The dormant stream routing is implemented, but physical EASession compatibility
is not tested or claimed. Stock V122 rejects the capability handshake in actual
emulation. The client refuses APPLY before sending any mutation to stock.

### Screen and phone storage

The app has a new "Extension RAM editor" screen. Its grid identifies all
36 switches by Base legend and matrix coordinate on each of the three layers.
The action sheet exposes keyboard usages `00..ff`, Esc/Tab buttons, Ctrl/Shift/
Alt/Cmd, modifier-only actions, shortcuts, disabled/transparent/OEM actions,
and hold/toggle/one-shot/tap-hold activation targeting any layer, including Base.
Native Symbols and Navigation have independent switches.

The UI explains the existing activation contract, not a new one. Tap-hold
selects its target immediately while pressed. A quick, uninterrupted release
can send the tap usage without modifiers. One-shot expires after 1000 ms.
Transparent on Symbols/Navigation resolves to Base, not another intermediate layer.

`ExtensionProfiles.json` stores typed named maps and the enabled flag separately
from `QMKProfiles.json`. Each file has a schema version. Invalid, corrupt, or
unsupported files are preserved and cannot be overwritten by Save. Profiles
survive app termination. Loading changes only the draft. Reconnect reads RAM.
After power loss, the user must load a profile and press Apply explicitly.

### Genuine V122 emulator workflow

`editor_server.py` binds only `127.0.0.1:8765` and starts the existing
`config-server` runner. It validates the image and source hashes against the
passing assurance report. It serializes extension packets and matrix scans,
records commands/replies, and retains the machine across client reconnects.
The power-loss control restarts the machine. The lost-ack control executes
APPLY but withholds its reply. Neither control accesses a physical device.

The simulator-only `V122EmulatorLink` exchanges bytes through that bridge and
delivers one-byte fragments to the production async adapter. Mapping, native
gestures, atomic apply, EP3 replies, and EP1 reports execute in V122. There is
no Swift or Python implementation of those firmware behaviors.

The updated protocol test uses production `StreamLink` partial writes plus
fragmented OEM/reply delivery. It tests stock rejection before mutation, the
full 978-byte record, truncated-response timeout, cancellation, disconnection,
concurrent-operation rejection, and lost-ack recovery without automatic retry.

The host editor workflow and Simulator UI workflow edit NAV Q at `(0,0)` to
Esc and SYM E at `(2,0)` to Tab. They apply/read back the map, execute native
gestures, and check complete HID reports:

- NAV+Q: `0000290000000000`.
- SYM+E: `00002b0000000000`.
- Release: `0000000000000000`.

The UI workflow also saves a named profile, terminates and relaunches the app,
demonstrates a different RAM map after power loss, requires manual reapplication,
and reconnects after a lost APPLY acknowledgement to read the actual committed map.
Screenshots and raw V122 traces distinguish this proof from physical compatibility.

Reproduce locally with normal Apple tools, without an offload wrapper:

```sh
# From keyboards/ck5200, with the existing pinned dependencies and stock fixture:
python3 stock_extension/assurance/run.py
# From the repository root, use an available iPhone Simulator UDID:
python3 ios/ClicksInspector/Tests/run-extension-editor.py --device <simulator-udid>
```

Omit `--device` for the host editor workflow only. Port 8765 must be free.
Evidence is written to `ios/ClicksInspector/build/extension-editor/`: host/UI
logs, screenshots, command/reply records, and per-boot V122 traces. The iOS
workflow has a separate editor job that retains this evidence. Hosted results
for this increment remain unverified until its workflows finish successfully.
Use a fresh `--output` directory if a previous UI result bundle already exists.

Local verification passed: the full Swift suite, 37 Python tests, native tests,
20 V122 assurance cases with 299 layer checks, asynchronous Swift-to-V122
integration, the host editor workflow, editor/ownership Simulator tests, the
existing QMK profile UI workflow, and an unsigned Debug iPhone build.
The editor evidence under `ios/ClicksInspector/build/extension-editor-verified/`
contains seven screenshots and 281 recorded command/boot/fault events. The host
and UI workflows each recover after one lost APPLY acknowledgement.

The separate exact-QMK binary assurance run was not repeated for this iOS change.
Its hash-pinned source gate is unchanged. The older local QMK build is not
accepted as a replacement image. The extension BIN still has SHA-256
`2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132`.

**Independent open gates:** full startup/USB enumeration/iAP2 authentication,
real interrupt latency and SysTick coalescing, keyboard-flash persistence, and
R3 independent recovery. Simulator success closes none of these gates.
M4 stays closed. The vendor inquiry remains unsent. No keyboard write occurred.

## 16. My Clicks product UX and separate RAM consent

Status: **local editor workflow and screenshot review passed. Hosted verification is not yet run for these changes.**
No firmware source or image change is part of this increment. No physical
keyboard command, firmware installation, or stock settings write ran.

The app opens into My Clicks, not the inspector menu. Advanced contains stock
evidence, diagnostics, firmware controls, and QMK tools. QMK tools appear only
for verified QMK identity or its explicit Simulator fixture.

The main screen separates device identity, the phone draft, named profiles,
and RAM synchronization. Save on iPhone and Apply stay in a bottom action bar.
Offline editing and saving never require a keyboard. Draft changes persist in
the existing versioned `ExtensionProfiles.json` file. Older profile libraries
remain readable. Invalid or unknown files are preserved. Reconnect never applies.
Native-mode switches explicitly describe the draft, not live hardware settings.

The action picker has searchable Keys, Navigation, Modifiers, Shortcuts, Layers,
and Native actions. It uses names instead of hex. Raw usages remain available
under Advanced. Modifier actions encode modifier bits, not keyboard usage slots.
The existing hold/toggle/one-shot/tap-hold engine contract is unchanged.

### Geometry evidence and its limit

The four cap rows, the Return position and width, and the bottom control order
follow the [vendor iPhone 15 product page](https://www.clicks.tech/en/products/clicks-for-iphone-15).
The inspected [key photograph](https://cdn.prod.website-files.com/66f575e72f06b9820f448d43/6721436d833799ec8c4f7210_Artboard%203.avif)
has SHA-256 `4b1bbb2c00da09d78e5a4a116e7c274d40d0e080bf075f095037e1a9519a2bce`.
The photo is reference evidence only, not a shipped app asset.

The photo proves geometry, not this unit's electrical routing. Letter and
ordinary-control routing use the V122 decode. Globe, Clicks, Shortcuts, and
Microphone are not assigned guessed matrix positions. The UI marks them
"Routing unverified" and exposes the four remaining decoded switches as
System action A/B/C and Native Control. The switches at positions 5, 11, 29,
and 35 remain editable without claiming which printed cap each belongs to.
All 36 decoded positions are represented once. Physical cap attribution
still needs independent evidence. Emulator success cannot establish it.

Keyboard buttons retain at least 44-point widths rather than shrinking text
to fit ten columns on a phone. The keyboard can scroll horizontally. Key list
provides full-width controls and is automatic at accessibility text sizes.
Labels use native fonts, meaningful VoiceOver names, and system colors for
light/dark mode. No 8-point coordinates appear on the main keyboard.

### Configuration policy is not flash policy

| Operation | Policy |
| --- | --- |
| Edit, autosave a draft, save/load profiles | Offline, no device required |
| Open an eligible stock keyboard | Exact known identity, automatic authorized version getter |
| Read extension capabilities | Explicit confirmation after eligible stock identity verification |
| Apply extension RAM map | Strict CKX1 handshake/readback plus separate per-session RAM consent |
| Apply arbitrary mappings to unmodified V122 | Unsupported, capability identity fails before any mutation |
| Firmware flash | Existing independent firmware controller and recovery gates, unchanged |

Capability reads do not grant RAM-write permission. RAM consent does not grant
flash permission. Session loss revokes RAM consent. No physical query or RAM
write is authorized for the agent by this implementation task.

### CI correction

The macOS editor job now runs `bash stock_extension/build_extension_image.sh`
before `python3 stock_extension/assurance/run.py`. This supplies the ELF, BIN,
source manifest, and `symbols.txt` before the hash-bound emulator executes.
The build and assurance sequence passes locally from a clean output directory.
The previous outputs were preserved, not deleted. All 20 assurance cases pass
and the extension image retains SHA-256
`2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132`.

The updated UI workflow exercises the new action catalog and main screen,
offline save, explicit apply, exact NAV+Q Escape and SYM+E Tab reports,
lost-ack recovery, relaunch and manual reapply after emulated RAM loss.
It also captures dark mode with accessibility text sizes. Hosted success
requires a new authorized publication and successful editor job, not an app
installation or the previous main iOS job alone.

Local acceptance evidence: `ios/ClicksInspector/build/my-clicks-ux-06/`.
The complete editor/ownership workflow passed. All 11 screenshots were
inspected, including real dark mode and maximum accessibility text size.
The action bar remains below one third of the screen height. Save and disabled
Apply remain readable. Status text scrolls at accessibility sizes, and the
keyboard becomes a full-width key list. Earlier passing tests did not hide
the rejected footer, tint, and search defects from screenshot review.
Existing recorder, stock-manager, and QMK UI workflows also passed under
Advanced. Their result bundles are under `my-clicks-ux-05/`; a clean
stock-manager repeat is under `my-clicks-ux-06/`. All three recorder source
exports exactly match their UI captures (`recorder-source-comparison.log`).
One earlier run retained a Simulator SpringBoard crash, not an app crash.
The stock-manager repeat passed without that attachment.

## 17. Explicit extension installer, 2026-10-23

Rudi requested an enabled installer with stronger checks and an explicit list
of weak evidence. This authorizes implementing and enabling the user-operated
flow, not an agent-executed physical flash. It does not establish R3 recovery,
authorize arbitrary firmware, reopen M4, or replace the pinned QMK gate.

### Entry and authorization

In the `ExtensionInstaller` configuration, use Connection, then
Install programmable extension. Default Debug/Release remain firmware-write
disabled. The installer configuration cannot install official stock or QMK
images. `StockFlashTrial` retains its separate stock-only permission.

The installer reuses `CustomFirmwareController` and its existing transaction,
reply parser, partial-write transport, durable journal, and audit log. There
is no second updater. Opening its screen or connection never stages or flashes.
The final confirmation requires a risk acknowledgement tied to the current
session generation. A new session invalidates that confirmation. A fresh exact
V122 version response is required immediately before A1.

The target must report CK-5200, hardware `1.2.0`, firmware `1.2.2`, and the
exact demonstrated stock version payload. The app must be active, the phone
must be charging/full or have at least 50% battery, and thermal state must be
nominal/fair. Staging checks these conditions again. The app prevents idle
sleep and disables normal navigation/dismissal during a transfer. These
controls cannot prevent force quit, cable removal, or power loss.

### Exact artifact and transfer

`Resources/ck5200_extension.bin` contains the unchanged 27,058-byte accepted
executable, SHA-256
`2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132`.
Preparation validates that exact size/hash and the original reset/vector checks.
The client adds 78 `FF` bytes, producing a 27,136-byte transfer, SHA-256
`438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2`.
This fills the application allocation without changing executable bytes and
avoids an unwritten final-page tail. It does not prove the OEM copy operation.
The per-frame boundary permits only this artifact in the installer build.
CI compares the bundled executable against the reproducible firmware build.

The transfer uses 848 ordered 32-byte data frames. The existing A1 maximum,
exact advancing A2 offsets, exact A3 committed size, single-operation guard,
session-generation guard, staging-only abort, and fail-closed audit remain.
Repeated install submissions return without changing the active transaction.
The journal records the actual padded transfer hash/size before A3. Lost or
invalid replies stop the operation. A lost A3 reply never triggers a retry or
A0. A reboot request and session closure never become a success claim.
An existing extension-install record blocks another flash in this flow.
Post-install verification still requires reconnect, stock identity, and an
explicit extension capability/map read. No layout applies automatically.

### Local verification and limits

- The existing scripted updater harness checks the exact padded transfer,
  consent, stale consent, damaged image, fresh identity, and lost A3 reply.
- The existing production-C staging harness exercises the full-area extension
  transfer and interruption cases. Its stock identity and installer copy are
  explicitly modeled. It is not original V122 updater or OEM installer proof.
- `extension-installer-ux-02/` contains a passing full editor/ownership workflow
  in the enabled configuration and 12 screenshots. The new installer screen
  loads the pinned artifact and stays disabled offline.
- `extension-installer-assurance/padded-runtime-report.json` records 15 runs
  of the exact padded image in genuine V122 execution. HID packets and coverage
  match the accepted unpadded executable. The original 20-case assurance report
  and unchanged firmware sources remain the baseline.
- Default diagnostics and M4 stock-only checks remain in `test.sh`. Signed and
  unsigned iPhone builds use normal Apple tools. Hosted CI requires publication.

The full Swift suite passed in `extension-installer-swift-verified-02.log`. This
includes ten production-C extension staging/fault cases. Those cases stop at
staging/reboot and never use the stock-only installer copy model to claim an
extension installation. All 37 Python tests and readiness checks also pass.
The held-staging duplicate-submit regression passes. The installer connection
permits only exact identity reads and the authorized transaction, not other stock getters.
The signed `ExtensionInstaller` build is installed on Rudi's iPhone 15 Pro Max,
bundle `com.rudironsoni.clicks-inspector`, version `0.2.0`, build `4`. Its signature,
pinned resource, and extension-only compilation flag were checked. No agent app
launch, physical keyboard command, or firmware flash ran. No changes were published.
Spoken VoiceOver behavior and a physical firmware install were not tested.

### Physical identity failure and thermal wording correction

Rudi's phone reported `Expected at most 8 bytes, got 9`. Read-only retrieval
of the existing app audit found `010008020300012001` and `000008020300012001`.
The audit contained rejected identity fragments, not update commands. No commit
journal was present. These are fragments with bytes before the version header,
not evidence that the version reply should be length nine.

The stream receiver previously discarded bytes before a request/receiver was
ready and passed each read directly to a single-reply accumulator. The fix
retains frame boundaries before requests, skips only recognized complete OEM
notifications, and keeps exact reply length/type/command/status checks. One
bounded parser now serves stock reads, firmware replies, and extension replies.
It never skips unknown bytes to search for a plausible reply. Input is not
consumed before the session receiver is installed. [INFERENCE] The observed
prefixes are notification tails. The missing preceding headers prevent proving
that interpretation from the old audit alone. A physical retry is still needed.

The message telling the user to cool the phone was not a temperature reading.
The installer now displays the exact iOS thermal-pressure category and updates
on battery/thermal notifications. Serious, critical, and unknown states still
block installation. Preflight logging records reported state, not temperature.
The full Swift suite, stock-receiver/ownership app tests, editor UI workflow,
and signed iPhone build pass. `stream-framing-fix-ux/` contains the new power
status screenshot. No agent-executed keyboard query or physical flash ran.

### Confirmation eligibility correction

Rudi subsequently reached confirmation with acknowledgement on but Flash
disabled. The UI gave no reason. The presentation used a Boolean and a separate
optional session value, then passed a Boolean eligibility snapshot to the sheet.
It now presents a session item directly and evaluates eligibility through a live
callback. The callback runs again before the confirmation action. A changed
session or failed preflight displays its reason. This does not bypass any
controller identity, power, image, consent, or transaction check.

The full Swift suite and the complete editor/ownership workflow passed in
`confirmation-fix-swift.log` and `confirmation-fix-ux/`. The latter includes
two view-only Simulator tests of the real confirmation component: acknowledgement
enables Flash for eligible state, and blocked state remains disabled with a
reason. No keyboard link exists in that fixture. Host tests separately check
session-item eligibility. A fresh read-only phone audit confirmed no update
transaction or commit journal before reinstalling. The signed fix is installed.
No agent-executed keyboard query or flash ran. Physical retry remains unverified.

**Low-confidence points, not closed by these tests:**

1. The OEM boot-stage copy routine, destination preservation, and interrupted
   commit/copy recovery are not observed. Independent recovery remains unproven.
2. Stock staging exposes no demonstrated end-to-end image hash/readback. Exact
   acknowledgements prove offsets and sizes, not byte integrity in keyboard flash.
3. Full startup, EP0 enumeration, real iAP2 authentication, interrupt timing,
   and electrical/power behavior are not proven by the bounded emulator.
4. Four system-cap routes remain unverified. This affects key attribution,
   not the transport checks. RAM layouts still need manual reapply after power loss.

No percentage for physical anti-brick confidence is supported by this evidence.

## 18. First user-run extension update and capability check

Rudi executed one physical extension update on 2026-10-23. The agent did not
send keyboard commands. Read-only retrieval of the phone's existing audit and
journal is preserved under `ios/ClicksInspector/build/post-install-evidence-01/`.
`analysis.json` verifies the captured client TX bytes and acknowledgements.

Verified from that capture:

- One A1 advertised `0x6A00` bytes.
- All 848 A2 frames have sequential offsets and exact advancing acknowledgements.
- Reconstructed client TX is 27,136 bytes, SHA-256
  `438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2`.
- A3 acknowledged `0x6A00`. A0 acknowledged an empty payload.
- The durable journal remains `sentRebootPending`, with reboot requested and
  the exact transfer hash/size. No second transaction appears in the audit.

These observations do not prove destination flash contents, execution of the
OEM copy routine, or execution of the extension. Rudi's subsequent screenshot
shows successful stock-version reading but a failed capability check. The
extension deliberately keeps the V122 version reply, so this does not establish
whether unmodified stock or the extension runs. The old audit lacks the actual
0x94 capability reply. Its framing-error message was not a valid stock diagnosis.

The app correction transfers the already verified EASession and its partial
frame state directly to the extension reader. It no longer disconnects and
reopens just to check capabilities. The existing strict capability identity,
nonce, generation, map, and separate RAM-consent checks remain. Short negative
0x94 replies report rejection status. Other malformed replies remain unverified
instead of being labeled stock. Exact extension requests, complete replies, and
framing failures append `extension.*` events to `StockUpdateAudit.jsonl`.
Normal OEM key notifications are not logged by this diagnostic hook.

Full Swift tests and genuine V122 protocol integration pass. The seeded-framer
test proves that a partial OEM notification survives ownership transfer. The
complete editor/ownership UI workflow passes under `post-install-fix-ux-02/`.
No firmware source or image changed. Before app replacement, a fresh capture
confirmed the same single transaction and no recent firmware command. The
signed app is installed. The journal before/after replacement matches byte-for-byte
in `post-install-evidence-02/` and `post-install-evidence-03/`.

### Captured capability rejection and offline install-path trace

Rudi completed two capability reads in the diagnostic app. Preserve
`ios/ClicksInspector/build/post-install-capability-evidence-01/` without changes.

| Request | Complete reply |
| --- | --- |
| `0c940100547becbf00000000` | `080294ff00000000` |
| `0c940100a60a007000000000` | `080294ff00000000` |

Both requests contain protocol 1, HELLO operation 0, and a different nonzero
nonce. Both replies reject command `0x94` with status `ff`. This is not a
parser failure. The app closes the session after the capability check fails.
The running firmware does not expose the expected extension capabilities.
The closed session therefore does not by itself show a physical disconnect.

Read-only offline checks reconfirmed one update transaction, 848 sequential
chunks and acknowledgements, and the exact padded transfer hash. The journal
matches `post-install-evidence-01/StockUpdate.json` byte-for-byte. Evidence hashes:

| File in `post-install-capability-evidence-01/` | SHA-256 |
| --- | --- |
| `StockUpdate.json` | `0d777832ab53b8e9b795a03c18521a1252a73e65004efbf49b2bca49b615192c` |
| `StockUpdateAudit.jsonl` | `2fbb76fe44654192d4dd17d10096d20b3c28fcf508a5e273f21ce1fd6f42b215` |

The offline trace used `.stock/audit/v122.dis`. All 6,655 encoded words in its
instruction/data listings match the pinned V122 binary. Relevant linked addresses:

| Address | Verified application behavior |
| --- | --- |
| `0x2208–0x2218` | Companion callback passes context `0x200000AC` to dispatcher `0x3216`. |
| `0x328A–0x32AC`, `0x37DC` | Unknown-command branch constructs `[08,02,command,FF,00,00,00,00]`, exactly matching the captured `0x94` replies. |
| `0x38FE–0x390C` | A3 passes the four-byte image length to the partial-page writer at staging marker address `0x08008A00`. |
| `0x3954–0x396C` | A0 sets bit 1 at context+0 and constructs `[04,02,A0,00]`. It does not copy application flash. |
| `0x3BB4–0x3BB8`, `0x2386–0x238E` | Main loop repeatedly calls `0x39FA` with that same context. |
| `0x39FA–0x3A1C` | Bit 0 saves the config record first and returns. The next loop can process the retained reset bit. No extra host command is required. |
| `0x3A20–0x3A32` | Bit 1 writes `0xBEEF0080` to PFIC register `0xE000E048` and spins. |

The retained OEM Android driver `p002a1.s.j` also ends with A1/A2/A3/A0.
Its `Reboot` state sends `[02,A0]` and declares success from the transport write
result. It does not read the A0 acknowledgement or verify the installed image.
This corrects the earlier shorthand that OEM success required an A0 ACK.
No additional host installation step is demonstrated by either retained path.

The actual client TX equals the bundled 27,058-byte extension plus 78 `FF`
bytes. Decoding its instruction at `0x2218` confirms a call to
`ck_extension_command` at `0x715C`, matching `symbols.txt`. The handler in
`ck5200_extension_shim.c` handles `0x94` directly. Its replies have at least
12 bytes and do not use status `ff`.

[INFERENCE] The observed requests reached an OEM command path instead of that
extension handler. This is not proof that all application flash stayed stock.
It also does not distinguish a failed copy, a partial copy, or another inactive-hook
condition. None of those causes is established by the captured acknowledgements.

The unknown boundary remains after the PFIC reset request. The stock application
files do not contain boot-stage bytes `[0x0000,0x2000)`. No trusted staging hash,
marker readback, destination readback, or copier execution trace is available.
Application acceptance of `0x6A00` does not establish boot-stage acceptance of
that length or image. Changing padding without that evidence would test a guess.
See [the existing OEM transition analysis](../../../research_notes/CK5200%20USB%20recovery%20feasibility/memory-ownership-and-oem-transition.md#23-device-side-control-flow-and-the-exact-unknown-boundary).

**Stop physical checks and update attempts.** Do not request another capability
read to reproduce this rejection. Do not reflash, reset, power-cycle as a test,
or clear/conclude the update journal. The next requirement is trusted evidence
for the missing boot-stage and flash state, not another UI or firmware change.
Rudi excludes contacting Clicks. Further investigation must use retained artifacts
and offline execution unless he separately authorizes another route.
No physical keyboard command, app replacement, or publication ran during the
preceding offline trace. That trace changed no firmware, app code, or saved evidence.

## 19. Post-transfer installation forensics, 2026-10-23

**Acceptance failed on the physical keyboard. Root cause remains unproved.**
This investigation separates submitted bytes, staging contents, reset execution,
and destination contents. It adds no keymap feature, UI redesign, firmware
change, vendor contact, hardware operation, or physical keyboard command.
The saved `sentRebootPending` journal remains unchanged.
Rudi subsequently confirmed that normal typing still works. This does not
establish extension execution. The missing function is full remapping.

### 19.1 Physical-event reconstruction

Source: the preserved `post-install-capability-evidence-01/StockUpdateAudit.jsonl`.
Times below are UTC, converted from the phone's Foundation `Date` values.
They are host observations, not measurements of the keyboard's reset signal.
The source hashes remain those in section 18.

| UTC on 2026-10-23 | Saved observation | What it establishes |
| --- | --- | --- |
| `06:56:09.291195` | One `trialStart` for the pinned 27,136-byte transfer | Host transaction started. |
| `06:56:09.365189` | A1 dispatched with length `0x6A00` | Requested staging length. |
| `06:56:09.367833` | A1 reply accepted | Application advertised `0x6A00`. |
| Before A3 | 848 A2 dispatches and 848 advancing replies | Submitted payload matches the pinned transfer. Not flash readback. |
| `06:56:11.214583` | A3 dispatched | Commit marker write requested. |
| `06:56:11.225189` | A3 reply payload `00006a00` | Application acknowledged its declared length. |
| `06:56:11.227195` | A0 dispatched | Reset requested through the application. |
| `06:56:11.231726` | A0 reply accepted with empty payload | Command acknowledged before any observed reset. |
| Not captured | Stream closure after A0 | No timestamp or cause can be reconstructed. |
| Not captured | Accessory detach/reattach after A0 | Hardware reset completion cannot be established. |
| Screenshot only | A subsequent V122 identity read completed | Application protocol remained responsive. No timestamped reply bytes survive in this audit. |
| `07:26:52.826076` / `07:26:52.827859` | First capability request/rejection | Complete `080294ff00000000` reply. |
| `07:26:52.844752` | `extension.closed`, detail `disconnected` | App protocol adapter closed after rejection. Not an EA detach event. |
| `07:27:33.223424` / `07:27:33.243560` | Second capability request/rejection | Same complete negative reply with a fresh request nonce. |
| `07:27:33.245267` | Second `extension.closed` | Same app-side close behavior. |

The update audit does not record EA attach/detach or stream lifecycle events.
`AccessoryInspector` sends those events to the separate stock recorder, whose
`record()` returns immediately when recording is off. The installer excludes
active stock recording. Its general log is an in-memory, 200-entry list.
The retained update snapshots contain only the update audit and journal.
A bounded scan of 11 retained stock/checkpoint JSON bundles found no events
in the attempt window `06:50–07:40` UTC. Older exports and Simulator bundles
cannot fill this gap. Missing events do not establish that no reset occurred.
No new recording or device retrieval was requested to fill the gap.

### 19.2 Staging-write acknowledgement contract

The complete trace uses pinned V122, SHA-256
`8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
All 6,655 encoded words in the retained disassembly match that binary.
Flash register names and bits match the retained CH32X035 SDK header
`external/ch32x035/EVT/EXAM/SRC/Peripheral/inc/ch32x035.h`.

| Linked address | Actual operation | Error/verification behavior |
| --- | --- | --- |
| `0x4956–0x4972` | Writes unlock keys to KEYR and MODEKEYR | Does not check that unlock succeeded. |
| `0x49D4–0x49FC` | Sets FTER, writes page address, sets STRT, polls STATR | Only tests BUSY bit `0x01`. No error result. |
| `0x4982–0x49A8` | Sets FTPG and BUFRST, polls STATR, clears FTPG | Only tests BUSY. No timeout. |
| `0x49AA–0x49D2` | Sets FTPG, stores one word, sets BUFLOAD, polls STATR | Only tests BUSY. No word comparison. |
| `0x49FE–0x4A24` | Sets FTPG, writes page address, sets STRT, polls STATR | Only tests BUSY. No page comparison. |
| `0x4974–0x4980` | Sets FLOCK | No success result. |
| `0x3174–0x31AE` | Calls these helpers, loading 64 four-byte words per page | No flash readback, error branch, retry, or status return. |
| `0x31B2–0x3214` | Reads the old page, overlays the requested bytes, calls `0x3174` | The read occurs before programming. The returned value counts overlaid bytes, not programming success. |
| `0x38CA–0x38D8` | A2 calls `0x3174`, then advances page state | Does not branch on a programming result. |
| `0x390C–0x3946` | A3 calls `0x31B2`, then constructs a zero-status reply | Does not verify the marker or resulting page. |

No helper checks STATR write-protection error `0x10` or end-of-operation
flag `0x20`. A controller that clears BUSY but leaves data unwritten is a
software counterexample to "ACK proves flash content": this call tree does
not detect that outcome. This is not evidence that such a failure occurred.
If BUSY never clears, the helper loops indefinitely and cannot acknowledge.
There is no software timeout or recovery in those loops.

Two additional contract corrections emerged from the same trace:

- **[CORRECTION] A1 sets the invalid marker in RAM, not immediately in flash.**
  The first full A2 page attempts to program that marker. A1 alone does not
  establish persistent marker invalidation.
- **[CORRECTION] A3 uses OR, not AND.** Branches at `0x378C` and `0x3792`
  accept either received-cursor equality or declared-length equality. The
  marker and acknowledgement use the stored A1 length. This attempt's host
  capture satisfies both equalities, so this is not an identified cause.

The [memory-ownership/OEM-transition note](../../../research_notes/CK5200%20USB%20recovery%20feasibility/memory-ownership-and-oem-transition.md#25-post-transfer-forensic-correction-2026-10-23)
now contains these corrections. The proven facts are submitted bytes and
application progression. Staging bytes, marker contents, and installed bytes
remain three separate, unverified device states.

### 19.3 Exact-image and fault analysis

`tools/post_transfer_forensics.py` reuses the existing V122 CPU runner.
It checks the saved transfer/journal, every patch-manifest word in the bundled
image, both actual request nonces, and a bounded fault matrix. It writes only
derived output. It never opens a device or changes an input artifact.

Reproduce from the repository root after the existing assurance build:

```sh
python3 keyboards/ck5200/tools/post_transfer_forensics.py \
  --evidence ios/ClicksInspector/build/post-install-capability-evidence-01 \
  --output ios/ClicksInspector/build/post-transfer-forensics-reproduced-20261023 \
  --ios-executable tmp/ck5200-tail-evidence/und/Payload/Clicks.app/Clicks
```

All six cases passed. The test's expected failure cases are not installations:

| Offline input | Observed result for the saved requests | Meaning and limit |
| --- | --- | --- |
| Exact uploaded image, including 78 `FF` bytes | Two 27-byte `CKX1` replies with the respective nonce | The bounded command path works when these bytes execute. No copier proof. |
| Unchanged stock V122 | Exact `080294ff00000000` twice | Original OEM path reproduces the physical reply. |
| Extension with only `0x2218` restored to its OEM call | Exact `080294ff00000000` twice | A missing command hook can produce the reply even with other extension hooks present. |
| Extension with its startup call restored to OEM SystemInit | Host guard reports `extension BSS not cleared` before requests | Detects skipped initialization under poisoned RAM. Not a measured guest response and not every possible RAM state. |
| Extension code region `[0x6900,end)` replaced by `FF` | Illegal-instruction fault at `0x70B2`, no reply | This selected corruption does not reproduce a working stock responder. Not exhaustive corruption coverage. |
| Extension command entry deliberately redirected to `0x3216` | Exact `080294ff00000000` twice | An OEM-equivalent bypass also reproduces the reply. This chosen mutation is not observed on the device. |

The normal image's `0x2218` call targets `0x715C`. Its `0x48CE` startup hook
targets `0x70B2`. The existing runner executes CRT initialization to SystemInit,
checks OEM data/BSS and extension BSS, and then uses synthetic configured USB
and authenticated companion state. It does not execute the unknown boot stage,
complete SystemInit, enumerate EP0, or authenticate against a physical iPhone.
These limits apply to the new probes as well as the old assurance report.

A follow-up probe checks the receive route before `0x2208`. In retained V122,
startup registers `0x2208` through `0x4776` and `0x6298`. The authenticated
receive adapter at `0x629E` invokes that callback at `0x62BC`, which reaches
the patched call at `0x2218`. A derived local runner executes the original
registration helper, checks its stored callback at `0x20000524`, and submits
both captured requests through `0x629E`. The exact padded image returns
`CKX1` twice. The removed-command-hook input returns the exact `FF` reply twice.
Local evidence is `ios/ClicksInspector/build/post-transfer-callback-route-20261023/`.
The original runner source remains unchanged. This checks the callback route
under synthetic authentication. It does not execute physical USB reception
or prove that the device contains these bytes. No bypass defect was found
in this retained route.

Static handler audit adds a control-flow boundary: for a valid request whose
command remains `0x94`, `ck_extension_command()` does not delegate to OEM.
It constructs a header of at least 12 bytes and uses status `0–4`, not `FF`.
Invalid request length returns without an extension reply. Ordinary capability
validation failure is therefore not the OEM eight-byte rejection path.
Arbitrary corruption of code, pointers, or request memory remains outside that
statement. The probes are falsifying examples, not an exhaustive proof over
every possible startup or memory fault.

Derived evidence is `post-transfer-forensics-reproduced-20261023/report.json`.
It records original hashes, runner hash, UTC timeline, exact replies, selected
mutation hashes, failure output, and explicit limits. Original audit and journal
bytes compare equal before and after replay.
The original-output overlap check also rejects output into the evidence directory.
All 37 existing Python tests, readiness checks, local link paths, and whitespace
checks pass. The forensic change does not rebuild firmware or rerun the full
Swift suite. No firmware or Swift implementation changed.

### 19.4 Retained iOS updater after A0

The retained unencrypted `Clicks` executable remains SHA-256
`d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b`,
with Mach-O `cryptid 0`. This pass targeted the EAF worker and its reboot path,
not the unrelated `PKService` Bluetooth command metadata.

| Address | Executable evidence |
| --- | --- |
| `0x10017B05C` | Worker calls A1 helper `0x100179BC4`, A2 helper `0x100179E54`, and A3 helper `0x10017A2B4`, branching on their Boolean results. |
| `0x10017A544` | A0 helper uses a static Swift byte array whose two bytes at `0x10084DF30` are `02 A0`. It uses the existing session's output stream and main-queue synchronous transport closure. |
| `0x10017A6C0–0x10017A718` | Helper tests the returned command byte against `A0`, returns a Boolean, and does not perform a destination check. |
| `0x10017B184–0x10017B194` | Worker calls A0, then immediately writes success without testing A0's returned Boolean. |
| `0x10017B1D4–0x10017B20C` | Worker resumes its continuation and returns. No additional install command, reset wait, disconnect wait, or post-reset identity check appears in this tail. |
| `0x1001CBF80–0x1001CC0F4` | Separate SessionHandler teardown closes both streams, removes them from the run loop, sets delegates to nil, and releases the stored session. It contains no installation command. |

The script checks the pinned executable, A0 call target, static request bytes,
and immediate success instruction. Objective-C stub selector references confirm
`close`, `removeFromRunLoop:forMode:`, `setDelegate:`, and `outputStream` for
the lifecycle trace. Local `ios-after-a0-windows.txt` preserves the exact windows.

**Result:** the traced iOS EAF worker has no consequential second installation
step after A0. Unlike the Android driver, it can select success even when its
A0 helper returns false. Our captured attempt received an A0 acknowledgement,
so that weaker OEM success policy does not explain this failure. This is static
worker parity, not proof of every dynamic model-selection route or physical
execution of the vendor app. No iPhone-specific missing install command was found.

### 19.5 Boot-stage evidence search

The six retained stock applications still start at linked `0x2000` and contain
no `[0x0000,0x2000)` bytes. This pass also inspected both retained IPA inventories
(312 entries each) and the APK inventory (1,183 entries). No boot/firmware-named
payload was found. The only `.bin` names were Apple's 23-byte ZIP metadata and
Kotlin's 1,738-byte `DebugProbesKt.bin`. Neither is a keyboard image.
A scan of 8–64 KiB archive members for an RV32 JAL prefix found no candidate.
This signature test cannot exclude an embedded, compressed, renamed, or
differently structured loader. Package hashes and results are preserved in
local `package-inventory.json`.

Public searches for `"CK-5200" bootloader firmware dump`,
`"Clicks" "CH32X035" bootloader dump`, `"CK-5200" firmware bootloader github`,
and `"iKeyboard_CK-5200" dump` returned no model-matched executable evidence.
GitHub code search for `"CK-5200" bootloader` also returned no result.
Generic CH32X035 or unrelated keyboard loaders are not evidence of this OEM
loader. This is a bounded negative search, not a claim that no dump exists.
No vendor contact, SDI research, or hardware acquisition occurred.

### 19.6 Separate CI reconciliation

At exact `fa4725a`, firmware run `39923157723` and the iOS `verify` job in
run `39923157707` passed. The `extension-editor` job failed. Its retained
`ui-workflow.log` identifies line 44, not a protocol or emulator-build failure.
The index-based back-button query selected the hidden My Clicks
`keyboard.connection` button beneath the installer sheet. The failure screenshot
and accessibility tree show a second navigation bar named `Extension installer`
with the actual `Connection` back button.

Commit `aa590fa59348b766d6f7ab849dc11937c213d5af` changes one test query to
`app.navigationBars["Extension installer"].buttons["Connection"].tap()`.
No app or firmware code changed. Local receiver tests and the complete enabled
editor workflow pass in `forensics-ci-selector-20261023/`.
Hosted [run `39932153737`](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39932153737)
passed at that exact commit. Both jobs passed: `extension-editor` completed at
`09:50:43` UTC and `verify` at `09:59:14` UTC on 2026-10-23. The hosted editor
step verified profiles, exact HID, and recovery in Simulator. The separate
`verify` job passed Swift tests, app builds, recorder/export checks, stock-manager
checks, and QMK profile regressions. This closes hosted software verification
of the test fix. It does not establish physical installation or recovery.

### 19.7 Decision boundary

The available evidence cannot distinguish silent staging/marker failure,
acknowledged A0 without completed reset, boot-stage image rejection or skipped
copy, and an installed but bypassed/corrupted hook. The bounded probes narrow
specific examples, not all physical realizations of those classes.

No demonstrated causal explanation identifies which class occurred. Changing
padding, repeating installation, resetting for diagnosis, clearing the journal,
or adding keymap/UI features is not a correction supported by these results.
The acceptance outcome remains: **submitted bytes verified; staging, reset,
and application replacement unverified; installed extension check failed.**
