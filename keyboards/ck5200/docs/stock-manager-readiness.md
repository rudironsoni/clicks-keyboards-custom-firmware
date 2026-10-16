# Verified stock firmware manager

Status: software verification in progress. Physical installation remains disabled. R3 is UNMET.
The [authoritative readiness record](flash-readiness.md) owns authorization.

## Scope and capability boundaries

This branch starts at accepted `main` revision `246f2b895232f2a585dd7b0eac3c3094a955c910`.
It reuses the iOS controller and accepted M2 C firmware. It does not replace the QMK port.
PRs #1, #2 and #3 remain separate and unmerged. No additional keyboard purchase is planned.

| Capability | Evidence and limit |
| --- | --- |
| Prepare official CK-5200 V122 | Exact size, SHA-256 and structural validation; no accessory required |
| Stock to stock | Production Swift against a decoded stock protocol simulation; C bridge stock mode is a version fixture over project firmware, not original stock C |
| QM/RS to stock | Production Swift against production M2 startup, QMK, iAP2, command dispatch, staging and SHA-256, with host hardware models |
| Application replacement | Explicit host installer model only; original installer remains unavailable |
| Unbootable to stock | Not demonstrated. M2 requires a functioning application-level service and cannot satisfy R3 |

Physical installation needs a separate authorization and recovery decision. No phone/keyboard operation, flash, reset experiment, ISP/SDI operation or protection change was performed for this milestone.

## Implementation

`StockFirmwareTransaction.swift` permits only the next byte-exact A1/A2/A3/A0 packet for the pinned image. State advances only after a valid acknowledgement. `CustomFirmwareController.swift` also binds that transaction to the session generation, exact model `CK-5200`, hardware `1.2.0`, and the observed identity: stock `01 20 01 22`, QM `51 4d 00 01`, or RS `52 53 00 01`. Stock additionally requires accessory firmware `1.2.2`.

Each update dispatch requires a one-use internal permit. Settings, keymap writes, rescue-entry writes and unrelated packets are refused. The host process exercises update permissions only with `STOCK_MANAGER_OFFLINE_TESTING && os(macOS)`. Every iOS configuration blocks writes, even with the old `STOCK_UPDATE_AUTHORIZED` flag. Apple controls physical ExternalAccessory authentication; host tests exercise the project C parser using modeled auth-chip responses.

The app stores an atomic transaction journal before A3. A3 acknowledgement advances commit state, never installation state. Reboot intent is separate. A missing, malformed or rejected response ends the attempt. Timeout and invalidation close the link and discard its output queue. The controller does not retry A1/A2/A3 in that session. Late replies and cancelled continuations cannot resume it. Reconnect only observes identity and retains uncertainty. The old manual verification API cannot erase a record. Legacy records remain readable; an unreadable journal blocks installation.

The accepted M2 implementation resets on A0 without an ACK. The controller records a request, not completed reboot or copying. Original stock has the decoded `04 02 a0 00` reply. A session closure, typing, or same-version response still cannot prove application replacement.

## Official app comparison

Input: locally supplied decrypted Clicks Companion 3.0.2 archive. The main executable SHA-256 is `d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b`.

The reproducible read-only check is:

```sh
python3 ios/ClicksInspector/Tests/verify_official_app.py <local-decrypted-ipa>
```

The check validates the pinned executable hash, Mach-O encryption command (`cryptid 0`), updater comparison words and static request constants. It neither extracts nor executes the app. Protocol instruction ranges were also inspected with Capstone 5.0.6; no proprietary executable or disassembly is published.

| Path | Official app evidence | Our controller |
| --- | --- | --- |
| A1 | Wrapper `0x100179bc4`; static prefix `06 a1` at `0x10084dfd0`; four-byte left-padded size appended; response byte 1 compared to `02`, byte 2 to `a1` at `0x100179dd4` | Same request framing; additionally checks full length, status and advertised capacity |
| A2 | Wrapper `0x100179e54`; chunk helper `0x10011201c` caps chunks at `0x20` at `0x100112184`; length is chunk size + 6; `a2` constant at `0x10084dfa0`; initial offset is four zero bytes at `0x10084df70` | Same 32-byte maximum and four-byte offset |
| A2 response | Opcode comparison `0x10017a0dc`; Foundation Data slice `[4..<8]` at `0x10017a0e4..0x10017a0f4` becomes the next offset bytes | Additionally requires the exact cumulative offset, rejecting stalls and jumps |
| A3 | Wrapper `0x10017a2b4`; prefix `06 a3` at `0x10084df40`; size conversion/left padding mirrors A1; opcode comparison `0x10017a4c4` | Additionally validates exact committed size and persists uncertainty before sending |
| A0 | Wrapper `0x10017a544`; full static request `02 a0` at `0x10084df10`; opcode comparison `0x10017a6c0` | Stock expects four-byte ACK; accepted M2 QM/RS has no ACK and remains explicitly unverified |

The inspected A1/A3/A0 wrappers require more than two response bytes and check response type/opcode. They do not check status byte 3 or a complete expected reply length in those wrappers. We retain stricter validation. Imported stub `0x1005802b8` resolves to Foundation Data byte subscript, and `0x1005802ac` to its range subscript. The common size helper at `0x100111a80` converts the integer through hexadecimal digits in order; callers prepend zero bytes to four bytes. This supports the existing big-endian size encoding.

This is a bounded static comparison, not execution of the official app, arbitrary-image authorization, a full app audit, or proof of an independent recovery path. No independent recovery path was identified in the examined evidence.

## Artifacts and checks

Official V122: 18,648 bytes, SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
The application package excludes `ck5200_qmk.bin`; it remains a negative host-test fixture.

CI builds unsigned iPhone and simulator `StockFirmwareManager` app archives and their SHA-256 list. It runs the preparation UI test in that same configuration and derived-data directory. Unsigned iPhone archives need local signing before installation; signing or phone installation is not part of this milestone.

Existing recorder, parser, stock transaction and M1 regression checks remain. Added checks exercise production Swift/C authentication, mode identities, transfer/commit, protected flash regions, failed marker commit, interruption after a written page, lost A1/A2/A3 replies, partial replies, expiry, session replacement after a successful ACK, reconnect authentication and journal retention. The real StreamLink test blocks A3 output and verifies timeout/invalid-response queue disposal.

The bridge uses one fresh process per mode/fault to reset C globals. Mode 0 overrides only version bytes; it is not a stock firmware emulator. `sm_model_install()` is explicitly a host copy model. `sm_disconnect()` includes explicit host staging cleanup for QM, which does not automatically abort on disconnect in accepted M2. These limits prevent interpreting host results as physical recovery evidence.

Local Xcode checks are blocked because this Mac lacks `external-ssd-root` and `xcode-offload`. The required doctor command failed with command-not-found before any Xcode command was run. Hosted macOS CI supplies Swift/build/simulator evidence. Firmware C tests and the official-app static checks can run locally.

## Independent review

Two independent reviews inspected code safety and test/evidence quality. Both closed their findings at `763a368` with no remaining blockers. Their review was static; hosted CI supplies runtime evidence.

- Safety: close and discard queued output after timeout/invalid response; invalidate task generations on close; check generation and cancellation after successful awaits; remove unsupported installation promises; UI-test the packaged configuration.
- Evidence: fail the second A3 page write to target the marker commit; interrupt after an actual staging page write; disconnect and require fresh authentication before observing stock after modeled installation.

The first CI attempt `38881900846` failed on the original incorrect commit-marker assertion. The correction tests the actual marker failure and expects an invalid `0xffffffff` marker, rather than weakening the production behavior.
Run `38882500615` passed the complete host suite, Debug iPhone/simulator builds and Debug UI tests. The manager test build then failed because the shared scheme also compiled Debug-only `@testable` unit tests in release mode. A dedicated manager UI-test scheme fixes the target selection without changing release testability or write permissions.

Passing revision, CI links and app hashes will be recorded here after verification. CI artifacts expire after 30 days.
