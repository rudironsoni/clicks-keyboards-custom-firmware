# M4: first physical stock firmware flash trial

Status: **SOFTWARE READY, TRIAL PREPARED. Physical execution requires Rudi's
separate explicit approval.** Prepared 2026-10-20, branch
`feat/ck5200-m4-stock-flash-trial`. Corrections pass applied 2026-10-21 after
Rudi's review; see "Review corrections" below.

This is the milestone the earlier sequence skipped: prove our application can
reinstall the official stock firmware on the real keyboard before any custom
QMK installation is considered. The immediate assignment (Rudi, 2026-10-20):
implement and verify the trial software, prepare the device trial, and perform
no physical firmware write without separate approval.

## Review corrections, 2026-10-21 (Rudi's M4 review)

All findings from the 2026-10-20 review were corrected before this record:

1. **Audit first-event overwrite (defect):** the initial audit write used two
   atomic writes, and the second (newline) destroyed the first event. Fixed to
   a single atomic write of record plus terminator. Regression-tested: a fresh
   log's first event must be `trialStart`.
2. **Audit completeness is now tested:** a fresh-log run must contain the
   COMPLETE first transaction, byte-for-byte and in exact order: `trialStart`,
   the A1 dispatch/accepted/reply triple, 583 ordered A2 triples with offsets
   advancing 0, 32, 64, … (final 24-byte chunk included), the commit-unknown
   point, the A3 triple, commit acknowledgement, reboot dispatch, and the A0
   triple. Failed exchanges retain their raw outgoing frame
   (`dispatch`/`dispatchAccepted`/`dispatchFailed` events), and the A0 reply is
   logged even while the session is torn down.
3. **Audit is fail-closed:** no update frame may be dispatched unless its
   audit entry persisted first (`auditUnavailable` aborts the trial). Tested
   by corrupting the audit path: zero frames reach the device.
4. **Abort regression:** the staging abort is exercised while the controller
   is deterministically stalled mid-A2 (no further A2, no A3, no journal
   record, control withdrawn after), and its refusal after the A3 boundary is
   asserted against the commit-unknown record.
5. **Post-trial reconciliation:** `concludePendingUpdate(observation:)`
   operationally closes a finished trial: it records the observation evidence,
   keeps the original outcome, never claims the application was rewritten,
   unblocks the next transaction, and is logged. `markPendingUpdateVerified()`
   still refuses; the UI offers "Conclude trial (evidence captured)".
   Host suite: **248 checks pass, zero failures**, including the full flash
   path compiled under the trial flag.
6. **V121 downgrade: NOT APPROVED** per Rudi's 2026-10-20 direction. It stays
   a documented option only; implementing it would be a separate decision and
   review.
7. **Signed-device validation:** the StockFlashTrial app builds for the
   physical iPhone (unsigned, host-verified). CLI signing is blocked by this
   machine's keychain ACL (`errSecInternalComponent` for non-GUI `codesign`),
   so the one remaining manual step is a GUI signing action:
   `ios/ClicksInspector/sign-trial-app.sh` prepares everything up to the
   keychain approval, and the fallback is Xcode GUI → Product → Run with the
   StockFlashTrial configuration. Device install, launch, banner, and consent
   checks then follow this procedure.
8. **Mac descriptor capture:** ran read-only; the CK-5200 was not attached.
   Rudi's step: plug the keyboard into the Mac and rerun
   `keyboards/ck5200/tools/mac_usb_capture.sh` (one command).

## What exists now

| Capability | State |
| --- | --- |
| Gated trial configuration (`StockFlashTrial`) | Implemented; builds for iOS simulator and generic iPhone; V122-only, identity-checked, QMK writes still impossible |
| Transaction flow (A1/A2/A3/A0) | Host integration suite passes byte-for-byte under the trial flag: **248 checks, zero failures** |
| Audit logging | Every exchange and lifecycle event persisted byte-for-byte (`StockUpdateAudit.jsonl`), fail-closed before dispatch, completeness-verified |
| Staging-only abort | Implemented, regression-tested mid-stall; refused once A3 is dispatched |
| Post-trial reconciliation | `concludePendingUpdate` closes a trial with evidence, never claiming a rewrite; blocks cleared honestly |
| Signed-device build | App builds for the physical iPhone; signing needs one GUI action (keychain ACL) via `ios/ClicksInspector/sign-trial-app.sh` or Xcode GUI |
| Mac read-only USB inspection | `tools/mac_usb_capture.sh` ran read-only; CK-5200 not attached; Rudi's one-command step |
| Physical execution | NOT PERFORMED. Awaiting approval. |

## The gate

Enabling the physical write requires ALL of the following:

1. **Compile-time:** the dedicated `StockFlashTrial` Xcode configuration
   (`STOCK_FLASH_TRIAL`). Default Debug and Release builds, and every other
   configuration, keep writes disabled exactly as before. The flag is the
   only iOS configuration that sets `stockUpdatesEnabled`, and the only image
   accepted is the bundled V122 artifact (SHA-256
   `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`,
   enforced in `PreparedStockImage` and again in the dispatch permit).
2. **Device identity:** connected target must report model `CK-5200`,
   hardware `1.2.0`, and stock firmware `1.2.2` for a stock re-flash.
3. **Runtime:** the trial build shows a red `STOCK FLASH TRIAL BUILD` banner,
   the install button requires an explicit risk-acknowledgement toggle, and
   the controller requires the exact per-frame dispatch permit.
4. **Journal:** any unresolved previous update blocks a new one until
   reconciled.

Arbitrary QMK flashing remains impossible in every configuration: A0-A3
dispatch requires the accepted transaction whose image hash must equal the
official V122 artifact.

## Evidence limits decided in advance

- The keyboard already runs V122. A successful same-version session proves:
  our transport, staging, commit-acknowledgment, and reboot-request behavior
  on real hardware, byte-for-byte with the audit log. It does NOT prove the
  boot stage rewrote the application flash. The trial report must claim only
  what was observed.
- Stronger replacement evidence exists as an option, requiring a separate
  decision: an observable firmware-version transition. The authentic V121
  image (`1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa`,
  from the same OEM manifest as V122) could be installed and verified through
  the identify/version getter changing `1.2.2` → `1.2.1`, then restored to
  V122 (`1.2.1` → `1.2.2`). This doubles the number of flash operations and
  changes the running firmware to an older version for one session. Default
  for the first trial is **V122-only**; the transition option is recorded for
  Rudi's explicit decision.
- Definitive destination evidence (trusted flash readback) requires either
  vendor cooperation or debug access; neither is authorized. Until then,
  "installation proven" remains unclaimable regardless of observed behavior.

## Risk assessment (from the 2026-10-20 memory-ownership investigation)

- A1/A2 staging writes only touch the device scratch area `[0x8A00,0xF500)`.
  Interrupted staging is harmless and retryable; the abort control covers it.
- The commit marker is written by A3; from that moment an interruption leaves
  a `commitUnknown` journal record, and the boot stage's behavior on the next
  power-up is UNKNOWN (marker retry semantics are unproven; see
  [memory-ownership-and-oem-transition.md](../../../research_notes/CK5200%20USB%20recovery%20feasibility/memory-ownership-and-oem-transition.md) §3.1).
- The worst case is a device that no longer boots its application. No
  recovery from that state is established. Mac fallback inspection
  (`mac_usb_capture.sh`) is prepared, but a Mac-based restoration path is NOT
  established; generic WCH tools erase destructively and remain excluded.
- This trial is a real flash operation with nonzero risk. It is not a
  diagnostic test and must not be described as one.

## Trial procedure (for review before approval)

1. Run the host suite: `bash ios/ClicksInspector/test.sh` (248 checks).
2. Build the trial app: `ClicksInspector` scheme, `StockFlashTrial`
   configuration, signed for Rudi's iPhone. Confirm the red trial banner.
3. (Mac fallback step, read-only) With the keyboard attached to the Mac,
   run `bash keyboards/ck5200/tools/mac_usb_capture.sh`; file the capture
   beside this record. Expected: VID `0x352E`, PID `0x2306`, three
   interfaces (iAP2 link, companion data, HID keyboard).
4. Reattach the keyboard to the iPhone; confirm the accessory session and
   stock V122 identity in the app.
5. Prepare the official image (button 1), then install (button 2) after the
   risk acknowledgement. Do not interrupt power during the session, and do
   not background the app: backgrounding closes the accessory session
   mid-trial (before A3 it only abandons staging; from the A3 commit point
   the journal record governs the state).
6. Record: complete `StockUpdateAudit.jsonl` (retrieved from the app
   container), the in-app phase messages, post-reboot reconnect behavior,
   typing test, and the retained `StockUpdate.json` journal state.
7. If any step fails or hangs: stop, capture the audit log and phase state,
   and file them. Do not retry automatically; the journal blocks unsafe
   retries by design. Reconnect and read identity only.
8. Abort rules: the staging-only abort control is valid before A3 only.
   After A3 the operation is committed-or-unknown and must be left to settle;
   observation only.

## Audit-event guarantees (review 2026-10-21, P3-5)

Fail-closed (a write failure aborts the trial before the frame leaves, or
fails the session): `dispatch` (before any update frame is sent, including the
direct A0 branch), `reply` (a reply that cannot be persisted fails the
session), and `rebootDispatched` (guards the direct A0 dispatch).

Best-effort (a write failure logs to the console and continues; the durable
safety record is unaffected): the lifecycle events `trialStart`,
`commitUnknownRecorded`, `commitAcknowledged`, `dispatchAccepted`,
`dispatchFailed`, `sessionClosed`, `replyRejected`, `updateFailed`,
`abortRequested`, and `trialConcluded`. The `StockUpdate.json` journal, not
the audit log, is the durable commit-unknown record: it is written before A3
and survives independent of the audit log.

## Independent review disposition, 2026-10-21

The scoped independent review of the physical-trial path (commissioned per
Rudi's 2026-10-20 direction) returned: **no P1 blockers; 2 P2 findings; 9 P3
notes; core gates confirmed** (no non-trial configuration can send A0-A3, no
image other than the exact V122 artifact can be staged or committed, abort
semantics safe and bounded, conclude-trial honest). The reviewer re-ran the
host suite independently.

Both P2 findings were fixed and regression-tested before this record:

- **P2-1** (the QMK/RS-to-stock A0 branch bypassed the fail-closed audit):
  the `rebootDispatched` write now guards the direct dispatch with
  `auditUnavailable`.
- **P2-2** (link closure, the realistic mid-trial interruption, was never
  recorded): a `sessionClosed` event is now logged when the link dies with an
  update in flight, with the reason and journal outcome; the first-transaction
  audit-order regression covers its position.

P3 notes fixed: `seekToEnd` errors no longer swallowed (P3-1); the
conclude/unblock/retention texts now state the one-trial-per-record UI
behavior and the journal-slot reuse honestly (P3-2, P3-3); rejected reply
fragments are persisted byte-for-byte as `replyRejected` (P3-4); this
fail-closed/best-effort table documents the audit guarantees (P3-5); the
stale check count corrected (P3-6); the duplicate scenario labels renumbered
(P3-7); the backgrounding warning added to step 5 (P3-8); the unused
`customFirmwareFlashingEnabled` constant removed and its comment corrected to
describe the structural enforcement (P3-9).

Host suite after the review fixes: **248 checks, zero failures.**

## Decision requested from Rudi

Approve or reject the physical execution of this procedure (V122-only), and
separately approve or reject the optional V121 transition for stronger
replacement evidence. No physical write happens before that approval is
recorded here.
