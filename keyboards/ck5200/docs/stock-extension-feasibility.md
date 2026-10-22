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

**Current evidence: section 12.** Rudi accepted `9113dab` only as an
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
| Image footprint | 23,702 bytes, current research build | 26,940 bytes verified |
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

1. Extend the enabled-mode comparisons in section 12 beyond their scripted
   states. Prove native preview/capture safety with nested IRQs. Verify
   layer activation choices before defining the runtime editor contract.
   OEM policy actions retain native gestures. They do not automatically
   select an engine layer number.
2. Extend execution beyond the synthetic handoff: full SystemInit/main
   execution, actual interrupt entry/return, USB enumeration and completion
   interrupts, suspend/reconnect, and companion transport backpressure.
   Current EP1 readiness is a modeled input, not a controller interrupt proof.
3. Verify FIFO overflow recovery and delayed/coalesced timer interrupts.
   The implemented overflow policy drains accepted reports, emits all-up,
   and mutes remaps until all physical keys are released. It is not lossless.
4. Only after executable equivalence passes: define the capability
   handshake, typed USB operations, readback, profile switching, and
   recoverable flash transactions. No iPhone editor or persistent profiles
   exist for this extension. A maximum record cannot fit in the 244 unused
   bytes of `0xF700`. No other storage region is approved for writes.
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
