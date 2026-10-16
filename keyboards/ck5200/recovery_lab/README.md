# R3 offline protocol laboratory

Real restoration is **BLOCKED**. This standard-library model has no device access,
transport, hardware commands, or firmware dependencies. It reads only the existing
V122 resource and requires its exact 18,648-byte length and SHA-256.

From the repository root:

```sh
python3 -B -m unittest discover -s keyboards/ck5200/recovery_lab -v
python3 -B keyboards/ck5200/recovery_lab/lab.py
python3 -B keyboards/ck5200/recovery_lab/lab.py --hypothetical
```

Default exit status is 2 (BLOCKED). The hypothetical scenario exits 0 on a model
match. Neither result establishes a real CK5200 restoration path.

## Evidence and assumptions

Read [ISP preservation](../../../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md)
and [boot entry](../../../research_notes/CK5200%20USB%20recovery%20feasibility/boot-entry.md).
The [2026-10-17 follow-up](../../../reports/CK5200%20USB%20recovery%20feasibility.md#2026-10-17-follow-up-outcome-2-exact-vendor-dependency)
requires vendor evidence. Staging fill does not establish the 40 destination bytes.
Factory identity fixtures use chip/type order, both observed vendor IDs, and the
specific G8U6/02.40, G8U6/02.60 and F8U6/02.60 trace combinations. These do not
identify the keyboard. F7P6 capacity conflicts and unpinned profiles block selection.
A4 has a count, no start address; its prefix footprint is a conservative inference.
The count-20 fixture represents pinned `wchisp` policy. The separately inspected
WCH library requests 19 for V122 with partial erase selected. Neither count proves
the target erase footprint. The lab does not execute either host implementation.
A5 decoding follows pinned `wchisp` commit `553327eeeea9fdc30414f1e5c74d1ff14b228a26`,
`src/protocol.rs:176-189`: u16 payload length, u32 offset, one padding byte, XORed
data. Decode does not grant execution. A6 compares, never reads back.
All factory execution is rejected. A3 and A7 are not implemented.

`Contract` holds counterfactual promises, not wire commands: bounded erase,
word-aligned monotonic program, documented compare/read, and independent entry
after power loss. **`tail_preserving_erase` additionally guarantees that excluded
tail bytes NEVER change, even during failure. This is NOT known CH32 page erase
and has no implemented wire operation.** Every capability defaults to false and
is required before the first mutation. Protection and entry remain separate gates.

The entire app `[0x2000,0x8a00)` starts invalid. Private synthetic device bytes are
oracle truth, not known host data or a backup. The 40-byte tail `[0x68d8,0x6900)`
requires the hypothetical read capability and must be readable. A frozen journal
binds the exact image, original tail, identity and contract. Operations must match
the next page/stage and exact image bytes. Failure revokes the active permit.

Only `[0x2000,0x68d8)` can change. Strict oracle checks include the tail during
success AND interruption, plus OEM, upper data, System FLASH, options, ESIG and
external state. Ordinary last-page erase appears only as a data-loss counterexample.
The restart fixture retains device memory but loses all host transaction state.
Recovery requires the original journal supplied from outside that host instance.
Simulator provenance checks its object identity; this is not a device journal or
hardware evidence. **No actual durable host journal is implemented.**
Image/strict-region oracle matches do not prove OEM execution or real recovery.
