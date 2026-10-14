# CK-5200 handoff

The authoritative current gate is [flash-readiness.md](keyboards/ck5200/docs/flash-readiness.md).
This replaces the obsolete first-flash instructions. Git history preserves them as historical evidence only.

Current task: install the diagnostics-only Clicks Inspector on Rudi's iPhone 15 Pro Max,
then collect a stock CK-5200 baseline using [the signing and baseline procedure](ios/ClicksInspector/SIGNING_AND_BASELINE.md).

- Physical M1 acceptance is UNVERIFIED. Host and simulator evidence is not physical acceptance.
- R3 independent recovery is UNMET. The Mac dispatcher depends on a running application and is not independent rescue.
- No custom image is authorized. No keyboard firmware write, reset, ISP operation, or protection change is authorized for M1.
- Default Debug and Release iOS builds are diagnostics-only at the controller boundary.
- The separately gated stock-update implementation is retained for later authorization, not for this baseline.
- Preserve installer `0x0000–0x1FFF`, application `0x2000–0x89FF`, staging `0x8A00+`, and upper reserved flash.
- Custom CH32X035 keymap storage is transient RAM. No flash-backed persistence is established.

Do not resume SDI research or replace this task with an architecture report. Collect observations from the stock case next.
