# CK-5200 handoff

The authoritative current gate is [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md).
This replaces the obsolete first-flash instructions. Git history preserves them as historical evidence only.

Stock diagnostics baseline: complete and accepted on 2026-10-14.
Five inspected sessions cover stock getters, mixed typing, exports, reconnect, and app transitions.
Use the acceptance scope, evidence hashes, and deferred gaps in the authoritative readiness record before choosing further work.

The [recovery decision](keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md) records NO-GO and the supporting offline audit.
The presumed installer region is not a proven immutable recovery service. A patched-stock reader cannot justify its own installation.
HTTPS inspection is stopped. The stock URL was already known. No phone settings changed, and the vendor inquiry remains unsent.
M2 [application-level rescue](keyboards/ck5200/docs/application-rescue.md) is COMPLETE, accepted by Rudi at `ee45b6bbb2b544f19d44c036099c4af2c5aa7156` after independent review.
Acceptance covers the software-only secondary mechanism, not physical compatibility, iPhone-operated restoration, or recovery from an unbootable application.
M3 verified stock firmware manager is closed for software scope in this R3 investigation.
Its historical delivery branch was `feat/verified-stock-firmware-manager`, based on `main` at `246f2b8`.
Tested revision `a7cf3f7` passed iOS CI `38883669172` and firmware CI `38883668796`. Both independent reviews are clear.
Unsigned app archives and hashes are published in the readiness report. Physical writes remain disabled in every iOS build.
See [stock manager readiness](keyboards/ck5200/docs/stock-manager-readiness.md) for implementation, verification and remaining limits.
The QMK/keymap/spare-procedure PRs #1, #2 and #3 remain separate and unmerged. Rudi has ruled out buying a second keyboard.
R3 remains unmet. The current task is evidence-first research and offline models on `feat/r3-usb-c-recovery-study`, based on `d981763`.
No physical experiment or hardware implementation may start without a separate decision.

- Comprehensive physical characterization remains UNVERIFIED beyond the recorded observations. Baseline collection is closed.
- R3 independent recovery is UNMET. The Mac dispatcher depends on a running application and is not independent rescue.
- No custom image is authorized. No keyboard firmware write, reset, ISP operation, or protection change is authorized for M1.
- Default Debug and Release iOS builds are diagnostics-only at the controller boundary.
- Every iOS configuration blocks physical updates, including `AuthorizedStockUpdate` and `StockFirmwareManager`.
- Preserve presumed installer `0x0000–0x1FFF`, application `0x2000–0x89FF`, staging `0x8A00+`, and upper reserved flash.
- Custom CH32X035 keymap storage is transient RAM. No flash-backed persistence is established.

Further recordings require a specific need in a separately scoped task. Do not resume SDI research.

## Current stopping point, 2026-10-20

**M4 (first physical stock firmware flash) is the current task** per Rudi's
2026-10-20 direction. The stock-to-stock sequence was always the first real
flashing milestone; the safety gate must no longer be a permanent stopping
point. The dedicated `StockFlashTrial` configuration, audit logging, and
staging-only abort are implemented and host-verified; the trial procedure and
risk record live in
[keyboards/ck5200/docs/m4-stock-flash-trial.md](keyboards/ck5200/docs/m4-stock-flash-trial.md).

The Mac operating model is now: iPhone primary (stock and QMK installs,
updates, keymaps, profiles), Mac emergency fallback (inspection first;
restoration only if later established). `tools/mac_usb_capture.sh` captures
USB descriptors read-only.

**No physical firmware write is authorized yet.** Executing the prepared trial
requires Rudi's explicit approval. The flash memory-ownership corrections and
the boot-stage questions (vendor inquiry 0a-0f, UNSG) remain the governing
evidence.

## Current stopping point, 2026-10-17

The focused follow-up selects **outcome 2: exact vendor dependency**.
See [actual WCH implementation analysis](research_notes/CK5200%20USB%20recovery%20feasibility/isp-loader-followup.md) and [V122 tail evidence](research_notes/CK5200%20USB%20recovery%20feasibility/v122-tail.md).
PROGRAM addressing does not establish selective ERASE. The 36 staging fill bytes do not establish the 40 destination bytes.
Static WCH library analysis found count-only A4, erase despite `IsEraseAllCFlash=false`, and configuration writes in the normal programming flow.
`IAPStartAddr` does not supply the X035 erase/program base. Target-side implicit erase remains unknown because no matching factory loader was recovered.
The [ready-to-send inquiry](research_notes/CK5200%20USB%20recovery%20feasibility/vendor-inquiry.md) remains UNSENT.
The hardware concept remains a bounded study. No adapter build or keyboard operation is authorized.

### Original study and offline review, 2026-10-16

The [USB recovery feasibility report](reports/CK5200%20USB%20recovery%20feasibility.md) synthesizes the three research notes.
It adds a conditional inner-plug BLE/USB-host concept, source-pinned ISP audit, alternatives, and eight falsifiable R3 criteria.
The [ADR extension](keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md#2026-10-16-extension-external-adapter-research-is-in-scope) supersedes the old next-M2 task.
M1, M2, and M3 stay closed within their recorded scopes.

Chip-level PC17 power-up entry is documented. Actual connector routes, early CC/data states, VDD timing, safe bias, and retained BOOT remain unknown.
The factory loader is mutable System FLASH, not immutable ROM.
The inspected `wchisp` programs from zero. No preservation-safe selective factory erase or USB RAM-helper route is established.
Protection state, the final page's 40 unknown bytes, all other device-specific state, and power-loss recovery remain critical blockers.
OEM startup and retained staging could also affect a directly restored application. Their behavior remains unknown.

Next: obtain model-matched vendor/source evidence. Both scoped independent agent reviews are closed.
The report writer inspected all three files in `keyboards/ck5200/recovery_lab/` and reran the checks.
All 10 unit tests passed. Captured CLI checks confirmed default exit 2 (blocked) and hypothetical exit 0 (model match only).
The final ISP/lab review closed all four findings: exact operation/image/state guards, strict tail preservation, restart journal requirements, and actual A5 decoding/rejection.
The hypothetical `tail_preserving_erase` contract is required before any mutation. It is not known CH32 page erase or a factory wire opcode.
The restart fixture loses host transaction state and requires the original in-memory journal. No durable journal or OEM normal-boot proof exists.
A1 identity fixtures, A4 count decoding, A5 packet decoding, and A6 comparison fixtures exist. A3/A7 and hardware transports are not implemented.
The coordinator reports no actionable ISP/lab blockers and no P1/P2 electrical blockers for conceptual scope, without hardware testing.
The final reviewer checked 10 existing tests, direct forbidden mutations, 11 A5 cases, and 292 fault/host-restart/retry combinations across all 73 pages.
Strict tail preservation passed. Missing, altered, and other-instance journals were refused. Default/hypothetical exits remained 2/0.
The [durable review summary](research_notes/CK5200%20USB%20recovery%20feasibility/review.md) records agent findings and limits, not a full transcript or vendor proof.
The expanded vendor query is UNSENT. A future generic-chip fixture requires separate authorization and cannot prove CK-5200 wiring.
No keyboard operation, firmware installation, reboot, probing, opening, ISP, unlock, purchase, or hardware build is authorized.
The report does not authorize flashing. [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md) retains NO-GO.
