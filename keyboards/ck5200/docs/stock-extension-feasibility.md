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

## 5. Open items for the next increments

1. Reconstruct the exact builder-callback invocation ABI from the scan path
   (`0x24AA–0x26D8` call site into the stored pointer) and the `a2` argument
   semantics at `0x2796`.
2. Compile and measure the three-layer engine (deliverable 2): tap, hold,
   toggle, one-shot activation; Esc/Tab/arrows/modifier/release correctness
   against the QMK port as reference.
3. Appended-code placement rules: the extension at `0x6900` must not collide
   with a page-rounded OEM copy of a larger stock image; document the
   assumption (a future official image larger than `0x6900` bytes overlaps
   the extension region and invalidates the layout — check against the
   manifest sizes at each update).
4. Behavioral equivalence harness: stock-default keymap → byte-identical
   reports vs. the unpatched OEM builder, run offline against scripted scans.

## 6. Demonstrated offline patcher (research artifact)

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
