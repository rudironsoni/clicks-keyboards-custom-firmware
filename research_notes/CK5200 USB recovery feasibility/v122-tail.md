# V122 final-page tail: destination contents remain unknown

> **SUPERSEDED 2026-10-20:** the preservation conclusion below is corrected by
> [memory-ownership-and-oem-transition.md](memory-ownership-and-oem-transition.md).
> Six official images were disassembled and diffed: every consecutive OEM
> update requires erasing the predecessor's final page (V121→V122 has 69
> erase-forcing bytes in `[0x6800,0x6900)`), V122 writes image data through
> V121's former tail, no stock version reads any address beyond its own image,
> and device-specific storage (serial, config) lives at `0xF600`/`0xF700`.
> The tail is unused application-area flash, not data that must remain
> unchanged. This note is retained as the record of the earlier bounded result.

Checked: 2026-10-17. Scope: existing local artifacts and static analysis only.
No device access, ISP execution, or firmware command occurred.

## Result

**BLOCKED:** we cannot classify destination bytes `[0x68D8,0x6900)` as meaningful data, erased padding, or installer-preserved data.
The file does not contain those bytes. The OEM installer below `0x2000` is absent.

**PROVEN (static application behavior):** V122 fills unused bytes in its **staging** page with `0xFF` before committing the staged image.
That staging tail has **36 bytes**, not 40, because staging starts with a four-byte length marker.
This does not prove what the missing installer writes to the destination page.

## Inputs and reproducibility

| Input | Length | SHA-256 |
| --- | ---: | --- |
| `ios/ClicksInspector/Resources/iKeyboard_CK-5200_V122_120.bin` | 18,648 | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |
| Local `.stock/iKeyboard_CK-5200_V121_120.bin` | 18,572 | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |

Both lengths and hashes were checked locally. No firmware bytes or complete disassembly are added with this note.
The repository's [ELF wrapper](../../keyboards/ck5200/tools/stock_audit/mkelf.py) places V122 at linked address `0x2000`.
Fresh disassembly used Homebrew LLVM with `--triple=riscv32 --mattr=+m,+a,+c,+xwchc`.
Scratch output is `tmp/r3-tail-followup/v122.dis`, excluded from publication.
Linked addresses below identify instructions in the hash-pinned input, not function names from recovered symbols.

## What the file and startup establish

| Evidence | Finding | Limit |
| --- | --- | --- |
| File length and base | V122 occupies `[0x2000,0x68D8)`. The final 256-byte page starts at `0x6800`, with 216 supplied bytes and 40 absent bytes. | Absence from a file does not establish erased device contents. |
| V122 startup `0x485C–0x4892` | `gp` resolves to `0x20000840`. Initialized RAM spans `[0x20000000,0x20000058)`. Startup copies 88 bytes from `[0x6880,0x68D8)`. | This loop does not read the 40-byte tail. It does not prove that all application or installer paths ignore it. |
| V121 length | V121 ends at `0x688C`, leaving 116 bytes to its page boundary. Neither archived version contains any byte at `0x68D8` or above. | Comparing these two files cannot recover the V122 destination tail. |
| Final 216 supplied V122 bytes | This region is not all `0xFF`. Its end includes initialized application data. | File termination is not evidence of a page-sized erased suffix. |

The existing [decode record](../../keyboards/ck5200/docs/STOCK_DECODE.md) identifies application data near the file end.
The [recovery audit](../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md) records the absent installer and unknown copy behavior.
The startup bounds above come from fresh instruction decoding, not from assuming the entire final page is padding.

## What the application staging code establishes

The application A1/A2/A3 commands below are **not** factory ISP A1/A2/A3.

| Linked instructions | Static effect |
| --- | --- |
| `0x37E2–0x3806` | A1 stores the image length, starts the page cursor at four, initializes the first word to all ones, and clears the staging-page offset. |
| `0x380A–0x38DC` | A2 appends supplied bytes after that first word. On each full 256-byte buffer, it writes a staging page starting at physical `0x08008A00` plus the page offset. |
| `0x38DE–0x38FA`, `0x3948–0x3952` | A3 fills the remaining partial staging buffer with `0xFF`, then submits the full page to the page-write helper. |
| `0x38FE–0x390C` | A3 writes the four-byte image length to physical `0x08008A00` through the read/modify/page-write helper. |
| `0x3174–0x31AE` | The page helper calls flash-control routines and supplies all 256 bytes from the buffer. |
| `0x31B2–0x3214` | The partial-write helper reads other bytes from the affected page, overlays the requested range, and calls the full-page helper. This is staging-marker handling, not proof of installer behavior. |

For V122, the staging layout derived from those instructions is:

```text
0x8A00             0x8A04                         0xD2DC       0xD300
| length, 4 bytes | image, 18,648 bytes            | FF, 36 bytes |

0x2000                                           0x68D8       0x6900
| destination image, 18,648 bytes                 | UNKNOWN, 40 |
```

**[INFERENCE]** A copier that skips the marker shifts the image by four bytes relative to staging page boundaries.
Copying all 40 destination-tail bytes would require bytes through staging address `0xD303`.
The observed A3 fill initializes only through `0xD2FF`. It does not establish the next four bytes.
Thus even a presumed full-page copy cannot justify assuming all 40 destination bytes are `0xFF`.

## Competing explanations and required evidence

| Explanation | Status | Evidence needed |
| --- | --- | --- |
| All 40 bytes are erased padding | **BLOCKED [UNVERIFIED]** | Model-matched manufacturing/installer contract defining all 40 bytes and their lifetime. A short image file is insufficient. |
| The installer preserves their prior contents | **BLOCKED [UNVERIFIED]** | Installer source, reviewed disassembly, or vendor copy specification covering partial destination pages and power loss. The application's staging helper is insufficient. |
| Some bytes hold meaningful or device-specific data | **BLOCKED [UNVERIFIED]** | Vendor storage map and references from the missing installer or other applicable code. No such role was identified in this bounded inspection. |
| The bytes are unused and may be discarded | **BLOCKED [UNVERIFIED]** | Explicit vendor classification plus a separate authorization decision. Do not silently expand the mutation range. |

The simulator keeps the tail excluded because its classification is unknown.
Its `tail_preserving_erase` capability remains an explicit hypothetical promise, not a claim about the factory loader.
If vendor evidence establishes disposable padding, the model and preservation policy would need separate review.
Until then, neither reading and rewriting the tail nor filling it with `0xFF` proves uninterrupted preservation.
