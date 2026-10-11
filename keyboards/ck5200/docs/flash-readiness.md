# Stock-reflash readiness report, 2026-10-12

Subject: the iOS app's stock-reflash path (the ONLY flash operation enabled
in this build), its offline evidence, and what a separately approved
stock-to-stock hardware experiment would and would not prove. Live flashing
of any kind remains blocked per `recovery-research.md`.

## What the app now implements (all offline-verified)

The controller refuses to stage any image except the exact authorized stock
V122 artifact (SHA-256 pinned, the same hash the preflight verifies), and
only onto a target that identifies as the expected CK-5200 stock unit
(model `CK-5200`, hardware `1.2.0`, firmware `1.2.2`, version payload
`01 20 01 22`). The UI removes the custom-flash buttons and the import path;
the controller gate is the enforcement, the UI removal is defense in depth.
Additional behaviors, each with a red-first regression in the committed
host checks:

- A1's advertised maximum and A3's committed size are validated from the
  reply payloads, not just the status byte.
- A2 acknowledgements must advance exactly by the sent chunk: zero
  progress, partial, backward, and forward-jump acknowledgements abort the
  transaction.
- A1/A2 timeouts retry once (lost request); an A3 timeout is never retried:
  it surfaces a commit-UNKNOWN outcome and records it.
- The unresolved-update record persists across reconnects and relaunches and
  is cleared only by explicit user reconciliation; a second flash is refused
  while it exists.
- A0's session death is treated as reboot evidence, never as installation
  success; the post-reboot state stays `updateUnverified` until verified.
- Rapid double submission starts exactly one transaction (synchronous guard).
- The stream adapter's partial-write accounting, fragment forwarding, and
  loss semantics are covered by a dedicated host check against the real
  controller.
- The full app type-checks against the iOS simulator SDK with zero errors
  (two pre-existing compile defects, a struct named `Self` and a
  file-private extension used across files, were found and fixed in the
  process: no repo check had ever compiled these files before).

Verification runs: `ck5200_flash_check` 59 assertions green;
`ck5200_stream_check` 12 assertions green; `ck5200_protocol_check` green;
full preflight 12/12.

## Evidence levels: what the live experiment would prove, and would not

The reviews require separating five levels; the experiment reaches only the
first four.

| Level | Meaning | Reachable |
| --- | --- | --- |
| Successful transfer | Every A2 chunk acknowledged at the expected offset, A3 acknowledged | YES, proven by the app's own exchange record |
| Acknowledged commit | The installer's A3 committed the staged length | YES, from the A3 reply |
| Observed restart | The session died after A0 as a reboot would cause | YES, observed |
| Functional firmware | The keyboard enumerates, identifies as V122, types, runs reads | YES, user-verified after reconnect |
| Independently verified installation | Byte-level proof the installer rewrote the application region | NO. There is no readback path: the stock protocol has no memory-read command (audited), the update dialect carries no end-to-end integrity, and reinstalling the SAME version cannot be distinguished from "nothing happened" by version reads alone |

Therefore the experiment would prove: the app-to-installer transaction works
end to end on the real unit, stock survives, and the app handles every
stage. It would NOT prove: byte-level installation, end-to-end integrity
(corrupted staged content is accepted by the real protocol; the committed
harness documents this honestly), or recovery from an unbootable
application. None of those is achievable through this protocol.

## Residual risks of the live experiment

- The transaction writes the only keyboard's staging region and resets it.
  The application region is untouched until the installer copies from
  staging on reboot. The stock writer verifies each staged page (decoded),
  and the experiment uses stock's own writer end to end.
- The installer at `0x0..0x2000` is unread; its behavior is inferred from
  the vendor's own shipping update path, which is exactly this path.
- The unresolved-update state means a same-version reinstall cannot be
  independently confirmed beyond function.

## Authorization boundary

This report is an input, not a request. The live stock-to-stock experiment
requires a separate explicit approval, and it is refused while an
unresolved-update record exists. Per the governing requirement, even a
flawless stock-to-stock outcome does not lift the recovery veto for custom
firmware: that depends only on `recovery-research.md`'s PC17 investigation
concluding in a demonstrated independent path, or an honest UNMET.
