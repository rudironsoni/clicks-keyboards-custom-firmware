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
R3 remains the next architectural priority, but no new milestone, investigation, or physical experiment may start without a separate decision.

- Comprehensive physical characterization remains UNVERIFIED beyond the recorded observations. Baseline collection is closed.
- R3 independent recovery is UNMET. The Mac dispatcher depends on a running application and is not independent rescue.
- No custom image is authorized. No keyboard firmware write, reset, ISP operation, or protection change is authorized for M1.
- Default Debug and Release iOS builds are diagnostics-only at the controller boundary.
- The separately gated stock-update implementation is retained for later authorization, not for this baseline.
- Preserve presumed installer `0x0000–0x1FFF`, application `0x2000–0x89FF`, staging `0x8A00+`, and upper reserved flash.
- Custom CH32X035 keymap storage is transient RAM. No flash-backed persistence is established.

Further recordings require a specific need in a separately scoped task. Do not resume SDI research.
