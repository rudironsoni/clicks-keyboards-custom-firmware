# CK-5200 recovery study review closure

## 2026-10-17 focused follow-up

An independent read-only review checked [V122 tail evidence](v122-tail.md) against the hash-pinned resource and fresh LLVM decoding.
It found no substantive correction. The review confirmed the startup copy bounds, four-byte staging marker, and 36-byte staging fill.
It also confirmed that the 40 destination bytes remain unclassified because the OEM copy code is absent.
The staging partial-write helper's full locator is `0x31B2–0x3214`.
These findings establish static application behavior, not installer execution or device contents.

Publication checks on 2026-10-17:

- `python3 -B -m unittest discover -s keyboards/ck5200/recovery_lab -v`: 10 tests passed.
- `python3 scripts/check-readiness.py`: passed.
- Captured default lab CLI: expected exit 2, explicit blocked result.
- Captured `--hypothetical` CLI: expected exit 0, explicit model-only result.

Firmware, iOS, hardware, and ISP runtime checks were not run for this documentation/offline-model scope.
The earlier electrical and simulator reviews below retain their original limits.

A separate publication review found two documentation issues, both corrected:

- The original ISP note allowed known tail bytes to be restored later. It now requires continuous preservation, consistent with the strict model.
- The note called the scratch source directory ignored. It now calls it untracked and excluded from publication.

That review found no new payloads, credentials, private paths, or authorization changes in its 14-file scope.
It excluded the then-pending WCH implementation follow-up and did not repeat the passing tests.

The subsequent independent [WCH binary review](isp-loader-followup.md) found no P1/P2 corrections in the inspected host claims.
It checked the library and adjacent header hashes. Fresh LLVM disassembly, symbols, and relocations matched the saved static outputs.
It confirmed count-only A4, the minimum-eight host rule for `0x23`, and counts 19 versus 62 under the two erase settings.
It checked the packed option offsets, the X035 branch's absence of IAP-base controls, and the separate IAP arguments' network-only use.
It also confirmed low-level A5 addressing, high-level zero-based programming, final empty A5, protection checks, and pre-erase configuration writes.

That review accepted **outcome 2** and the focused vendor inquiry within this evidence scope.
It did not repeat archive searches or verify download provenance online.
It could not verify target-loader effects or the keyboard's protection state. No WCH code ran and no device was accessed.
Both focused technical reviews are closed. The original electrical/model reviews remain separate from these host and tail findings.

## Original study reviews, 2026-10-16

Date: **2026-10-16**. **Both independent agent reviews are closed for the conceptual research and offline-model scope.**
The final ISP/lab reviewer reported all four initial blockers closed, with no actionable blockers remaining.
The electrical reviewer reported no P1/P2 blockers for the conceptual scope.

This is a durable summary of reviewed agent findings supplied by the coordinator, not a full review transcript.
The reported review checks below are not new vendor facts or physical observations.
They are distinct from the report writer's earlier local rerun of the 10 tests and two CLI scenarios.
No new tests or hardware operations ran to create this summary.

## Initial ISP/lab findings and fixes

| Initial finding | Fix reviewed | Closure evidence reported by final reviewer |
| --- | --- | --- |
| Operation authorization was too broad | Require the exact next operation, page, image bytes, identity, contract, and active state. Rejection revokes the permit. | Direct tail-write, wrong-image, stale-operation, and BLOCKED-state checks refused mutation. |
| Tail preservation did not satisfy the strict failure requirement | Require `tail_preserving_erase` before any mutation. Excluded bytes remain unchanged during modeled erase, program, compare, and lost-transfer faults. Ordinary whole-page erase is only a counterexample. | Strict preservation of all 40 tail bytes passed across the reviewed fault/restart/retry cases. |
| Restart retained host state or overstated journal persistence | Remove host transaction state on restart. Require the original journal through simulator provenance before retry. | Missing, altered, and other-instance journals were refused. No durable journal claim remains. |
| Program rejection did not exercise actual A5 framing | Decode source-derived A5 length, offset, padding, and encoded payload before range checks. Reject malformed/excluded packets and deny execution even for valid packets. | Eleven malformed, excluded-range, and valid-but-execution-denied A5 cases passed. |

Implementation: [lab.py](../../keyboards/ck5200/recovery_lab/lab.py).
Existing regression tests: [test_lab.py](../../keyboards/ck5200/recovery_lab/test_lab.py).
Scope and model assumptions: [README.md](../../keyboards/ck5200/recovery_lab/README.md).

## Final ISP/lab review result

The reviewer independently ran the **10 existing unit tests** and reported success.
Additional review checks covered **11 A5 cases** and **292 fault/host-restart/retry combinations**, from four stages across all 73 application pages.
The stages were erase, program, verify, and lost transfer.
The reviewer also checked direct forbidden mutations and strict tail preservation, as recorded above.
These additional counts describe reviewer checks, not extra committed unit-test methods or hardware trials.

The default CLI returned **2**, with real restoration blocked.
The hypothetical CLI returned **0**, with the model's image and excluded-region checks satisfied.
The reviewer reported **all four initial blockers closed and no actionable blockers remaining** within this scope.

The limits remain explicit:

- `tail_preserving_erase` is a hypothetical contract, stronger than known CH32 page erase. It is not a factory wire operation.
- Journal provenance uses simulator object identity. It is not durable storage, crash-safe persistence, or production restart recovery.
- A1 identity fixtures, A4 count decoding, A5 decoding, and A6 comparison fixtures exist. A3/A7 protocols remain unimplemented.
- The model does not execute the OEM installer or CPU boot. It proves no physical preservation or independent keyboard recovery.

The [ISP research](isp-preservation.md) retains the actual loader/protection/erase evidence and its limits.
The review does not convert the hypothetical contract into a supported loader capability.

## Final boot, package, and electrical review result

The reviewer independently inspected existing photos [09](../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-09.jpg),
[22](../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-22.jpg), and [24](../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-24.jpg).
The review confirmed the 48-lead count and checked the conditional C8T6 pin map against the datasheet.
It did not establish the unreadable part marking or connector continuity.

The reviewer checked the official WCH datasheet/reference-manual hashes and primary-document claims.
These included the unknown BOOT sample window, mutable BootAsUser region, and reference circuit's 4.7 kΩ pull-up to its own MCU supply.
The reviewer recomputed the stated numeric electrical bounds and validated the exact candidate parts, USB-C requirements, and USB-host documentation.
The result was **no P1/P2 blockers for the conceptual scope**, with no hardware testing.

The detailed source versions, hashes, pages, calculations, and limitations remain in [boot-entry.md](boot-entry.md) and [electrical.md](electrical.md).
Review closure does not identify the CK-5200's hidden nets, internal VDD timing, actual protection, or installed System FLASH contents.
It does not make the candidate circuit fabrication-ready.

## Readiness and next step

The [engineering report](../../reports/CK5200%20USB%20recovery%20feasibility.md) contains all seven deliverables and the eight R3 criteria.
[flash-readiness.md](../../keyboards/ck5200/docs/flash-readiness.md) remains the sole authorization authority.
**R3 remains UNMET. Custom flashing remains NO-GO.** Review closure applies to the investigation, not the missing recovery mechanism.

The next step is model-matched vendor/source evidence. The vendor inquiry remains **UNSENT**.
A generic-chip fixture remains conditional on separate authorization and cannot prove CK-5200 wiring.
No device access, publication, commit, purchase, or hardware build follows from this review summary.
