# M4: first physical stock firmware flash trial

Status: **SOFTWARE READY, TRIAL PREPARED. Physical execution requires Rudi's
separate explicit approval.** Prepared 2026-10-20, branch
`feat/ck5200-tail-ownership-evidence`.

This is the milestone the earlier sequence skipped: prove our application can
reinstall the official stock firmware on the real keyboard before any custom
QMK installation is considered. The immediate assignment (Rudi, 2026-10-20):
implement and verify the trial software, prepare the device trial, and perform
no physical firmware write without separate approval.

## What exists now

| Capability | State |
| --- | --- |
| Gated trial configuration (`StockFlashTrial`) | Implemented; builds for iOS simulator; V122-only, identity-checked, QMK writes still impossible |
| Transaction flow (A1/A2/A3/A0) | Host integration suite passes byte-for-byte under the trial flag: 219 checks, zero failures |
| Audit logging | Every update exchange and lifecycle event persisted byte-for-byte (`StockUpdateAudit.jsonl`), validated by host checks |
| Staging-only abort | Implemented; refused once A3 is dispatched |
| Mac read-only USB inspection | `tools/mac_usb_capture.sh` ready for Rudi to run with the keyboard attached |
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

1. Run the host suite: `bash ios/ClicksInspector/test.sh` (219 checks).
2. Build the trial app: `ClicksInspector` scheme, `StockFlashTrial`
   configuration, signed for Rudi's iPhone. Confirm the red trial banner.
3. (Mac fallback step, read-only) With the keyboard attached to the Mac,
   run `bash keyboards/ck5200/tools/mac_usb_capture.sh`; file the capture
   beside this record. Expected: VID `0x352E`, PID `0x2306`, three
   interfaces (iAP2 link, companion data, HID keyboard).
4. Reattach the keyboard to the iPhone; confirm the accessory session and
   stock V122 identity in the app.
5. Prepare the official image (button 1), then install (button 2) after the
   risk acknowledgement. Do not interrupt power during the session.
6. Record: complete `StockUpdateAudit.jsonl` (retrieved from the app
   container), the in-app phase messages, post-reboot reconnect behavior,
   typing test, and the retained `StockUpdate.json` journal state.
7. If any step fails or hangs: stop, capture the audit log and phase state,
   and file them. Do not retry automatically; the journal blocks unsafe
   retries by design. Reconnect and read identity only.
8. Abort rules: the staging-only abort control is valid before A3 only.
   After A3 the operation is committed-or-unknown and must be left to settle;
   observation only.

## Decision requested from Rudi

Approve or reject the physical execution of this procedure (V122-only), and
separately approve or reject the optional V121 transition for stronger
replacement evidence. No physical write happens before that approval is
recorded here.
