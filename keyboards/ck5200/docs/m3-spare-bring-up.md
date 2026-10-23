# M3: spare CK-5200 bring-up procedure

Status: **procedure only; no hardware work executed or authorized.**
M1 and M2 remain accepted. [R3 remains UNMET and physical flashing remains NO-GO](flash-readiness.md).
This procedure applies only to a separately acquired, explicitly designated test unit. Never substitute the user's sole keyboard.

## 1. Entry conditions and stop rules

Use an exact `CK-5200`, preferably reported hardware `1.2.0`, matching the accepted stock baseline.
`CK-5200B`, CP-1000 Power Keyboard, and other model suffixes are not interchangeable.
A seller's description or USB VID/PID alone does not establish the model, hardware revision, or internal board identity.
No spare has been acquired or verified by this software task.

Before connecting a spare for this procedure, obtain a separate decision covering that unit and the permitted observations.
Before any write, also obtain explicit authorization for the exact unit, candidate hash, installation method, restoration method, and permitted failure cases.
Authorization for ordinary bring-up is not authorization for corruption, interrupted copying, protection changes, ISP/SDI, or reset experiments.
Keep the sole keyboard physically separate from the test station. Record a private unit identifier; do not publish serials or authentication material.

Stop if the model/revision differs, protection state is unknown for a proposed programmer operation, backup scope is incomplete for the proposed operation, the restoration method is not established, or device selection is ambiguous.
Stop after an unexpected disconnect, timeout, identity change, or failed readback. Preserve evidence before retrying; never interpret silence as success.
This document supplies no authorized flash or erase command. The executable installation procedure is blocked until a real restoration route is established.

## 2. Freeze the software and evidence pack

Run these offline steps from the repository root on the reviewed candidate commit, without a keyboard attached:

```sh
git rev-parse HEAD
git status --short
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/build.sh
bash keyboards/ck5200/scripts/test.sh
python3 scripts/check-readiness.py
shasum -a 256 keyboards/ck5200/build/ck5200_qmk.bin \
  keyboards/ck5200/build/ck5200_qmk.elf \
  keyboards/ck5200/build/ck5200_qmk.map
```

Use the [first-boot candidate record](m3-first-boot.md) for exact build dependencies, image size, hashes, regressions, and remaining hardware assumptions.
Retain the binary, ELF, map, toolchain/dependency revisions, build and test logs, CI links, and source commit together.
Check the download-only stock fixture before the harness; setup/build requirements are documented in [BUILD_AND_FLASH.md](BUILD_AND_FLASH.md).
The offline suite requires archived stock fixtures. A missing fixture blocks that test; it is not a reason to query or write a keyboard.
Do not rebuild or substitute an image after its hash is authorized without a new artifact decision.

The restoration reference is exact `CK-5200` V122, 18,648 bytes:

```text
https://xinyi1.clicks.tech/CK-5200/iKeyboard_CK-5200_V122_120.bin
SHA-256 8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8
```

The vendor manifest is `https://xinyi1.clicks.tech/firmwares.json`.
Select by exact model equality and verify the complete payload hash. Do not use a model-name prefix.
The downloaded application is not a full backup of a keyboard, its installer, serial/configuration pages, or authentication chip.

## 3. Identify and preserve the spare

After separate authorization for read-only observations:

1. Record the test-unit label, case/connector type, purchase provenance, visible model markings, phone model/iOS version, and app build.
2. With original firmware still running, collect USB descriptors and the EA model, hardware revision, firmware revision, and protocol list. Expected baseline: `CK-5200`, `1.2.0`, `1.2.2`, `com.clickscompanion.protocol`. Preserve mismatches and stop.
3. Use the existing diagnostics-only workflow to record the seven stock getters and a short typing/release sample on the spare. This establishes a spare baseline; it does not reopen M1 on the sole keyboard.
4. Save the observed settings and public descriptors. Keep any unique identifiers private. Compare them after restoration.
5. Inventory what can be preserved through documented read-only means. Mark each region **read and verified**, **known reference only**, or **unavailable**. Do not call the backup complete if boot code, protection state, or device-specific data is missing.

| Storage boundary | Preservation requirement |
| --- | --- |
| Presumed installer `0x0000–0x1FFF` | No application package contains it. Its implementation and protection remain unverified. No overwrite or extraction experiment is authorized. |
| Application `0x2000–0x89FF` | Archive the exact original application if obtainable; distinguish a vendor reference from a readback of this unit. |
| Staging `0x08008A00–0x0800F4FF` | Preserve any known transaction state; do not erase it merely to make the test start cleanly. |
| Serial/configuration `0x0800F600` / `0x0800F700` | Device-specific; never replace with values from another unit or with zeros. Full device-specific storage coverage is still unknown. |
| Upper reserved space, option/protection state, authentication chip | Unknown contents are not disposable. Do not clear protection or extract credentials as part of this procedure. |

Use [HARDWARE.md](HARDWARE.md) and the [recovery ADR](adr/0001-independent-iphone-recovery.md) for evidence limits. Do not infer safe readback from pad labels or a generic WCH example.

## 4. Establish restoration before installation

Record a concrete restoration method and its evidence, not a fallback wish:

- Which component runs when the candidate application does not start?
- What exact entry condition, host hardware, transport, and software does it require?
- Which complete image and device-specific data does it accept or preserve?
- What evidence proves bounded writes and what happens if power is lost?
- Has this method actually restored a matching test unit? Cite its identity and logs.

If only the running application can accept A1/A2/A3/A0, restoration after a bad application is **not established**. Stop before installation.
A vendor-supported service or a separately authorized programmer route could reduce risk for the spare, but programmer/Mac-only recovery does not satisfy iPhone-only R3.
If a proposed method needs read-protection removal or mass erase, stop for a new decision stating exactly what would be lost. Do not try it as an exploratory step.

The gate remains blocked today: no independent CK-5200 restoration route has been demonstrated.
An inability to demonstrate a route without a destructive experiment requires a separate bounded investigation decision for the spare. This procedure does not supply that authorization.

## 5. Controlled candidate installation, after a separate decision

The approved operation record must contain the exact host/app build or command, transport, unit selector, image hash, expected responses, timeout policy, and stop/recovery actions.
Review it before execution. Ordinary diagnostics and QMK-keymap app configurations cannot install firmware; do not enable stock-installation permissions to bypass that boundary.
The official app's normal updater is a protocol reference, not proof it accepts arbitrary custom images.

During the separately authorized transaction, record A1 size/capacity, each A2 acknowledged next offset, A3 outcome, and the A0/reset transition separately.
An A3 timeout is an unknown commit outcome. Do not automatically resend A3 or reboot.
Do not run M2 entry, reset, unplugging, or power interruption experiments during this first installation.

## 6. Observe first boot and normal operation

After an authorized installation, complete each observation in order. A failure stops expansion of the test scope.

1. **Enumeration:** record raw USB descriptors, interfaces, endpoints, identity and serial source. Compare against the frozen candidate. A visible USB device does not prove HID or EA functionality.
2. **Application identity:** obtain the exact QM identity using the approved app. Reject stock, RS rescue, unknown or mismatched hardware. Preserve raw response bytes and app logs.
3. **EA session:** open `com.clickscompanion.protocol`; record both streams opening and a successful validated command. Apple hides the MFi exchange; a stream-open event alone is not transaction success.
4. **Typing:** use a blank local text field. Test every physical crossing from the frozen matrix table, one press and one release. Record the visible cap and row/column separately; mark ambiguous mappings unverified. Release all keys between trials.
5. **Modifiers:** test each modifier alone and with a letter, in both press orders. After release, type a plain letter. Check for stuck modifiers and missing releases using available HID/OS evidence.
6. **Layers:** test the compiled base, SYM one-shot, Clicks navigation, and fourth layer exactly as the candidate defines them. For a layer without a factory activation key, use a saved RAM test profile and a reversible binding through the approved QMK editor. Record that change; do not claim stock behavior. Verify the one-shot affects one intended key and returns to base.
7. **Consumer usages:** test the mapped system/media action and release. A shortcut consumed by iOS is recorded as an OS observation, not inferred raw HID bytes. Mark unavailable raw reports unobservable.
8. **Rapid typing:** type a non-private fixed sample for 60 seconds, including alternating keys and overlapping modifiers. End with all keys released and a plain character. Record repeated, missing, or stuck keys.
9. **Disconnect and resume:** only after normal bring-up and separate approval for these observations, exercise idle/wake, app background/foreground, ordinary detach/reattach, and phone lock/unlock. Record unresolved presses. On reconnect, require fresh QM identity and keymap comparison before offering profile reapplication.
10. **Charging:** using the case's supported charger/port, record visible charging with typing and EA transactions before and after connecting the charger. This is a functional check, not an electrical or thermal certification. Stop on abnormal behavior.
11. **Profiles:** save a named four-layer phone profile, apply a small controlled change, verify each write by readback, reconnect and compare, then reapply only by explicit user action. Demonstrate partial-failure reporting only in the offline Swift/C regression harness; do not inject hardware faults here. If all physical steps pass, restore factory defaults and read them back before ending the trial. A real timeout or failed readback stops the trial: preserve the evidence and send no further writes, including factory reset, until a separate recovery decision.

The simulator cannot establish these physical results. Do not transfer host-test pass marks into this table.

## 7. Restore official V122 and distinguish the results

Restoration is a separately approved operation with the verified V122 artifact, exact spare identity, and established route from section 4.
Record transfer, commit, installer execution evidence, rebooted application identity, HID behavior, EA/getter behavior, and preserved unit settings/identity independently.
The original baseline must match after restoration, or the result is incomplete.
Returning to a working V122 from a working QMK application is **normal restoration only**. It does not demonstrate recovery from an unbootable application.

| State | Minimum evidence to mark demonstrated | Does not establish |
| --- | --- | --- |
| Firmware transferred | Complete sent bytes and acknowledged offsets match the selected image | Commit, install, or boot |
| Firmware committed | Valid complete A3 acknowledgement; record timeout as unknown | Original installer execution |
| Installer executed | Direct evidence of the original copy/verification path, or label the conclusion `[INFERENCE]` | Successful application boot or interruption safety |
| Application booted | New application responds with the expected identity after reset, with matching artifact provenance | Full HID, EA, or charging compatibility |
| USB HID functional | Recorded key/modifier down/up behavior and correct report/OS outcomes | EA session functionality |
| iPhone EA session functional | Compatible protocol session plus a validated command/reply | Independent recovery |
| Official firmware restored | V122 identity, stock getters, typing/releases and preserved device-specific state match the pre-test baseline | Recovery when application code is unusable |
| Recovery demonstrated with application unbootable | Authorized invalid-application case restored via independent recovery, with evidence that application code did not provide the service | iPhone-only R3 if a programmer, Mac, or debug pads were required |

Use `not run`, `pass`, `fail`, `unknown`, or `[INFERENCE]` for each row, with timestamped evidence references. Never collapse the rows into one "flash succeeded" result.

## 8. Failure cases require a new authorization

Do not deliberately corrupt vectors, erase application pages, interrupt staging/commit/copying, or disable a transport during this task.
Any later spare-only failure campaign must specify one bounded case at a time, the independent restoration route, expected preserved regions, and a stop condition.
Separate interrupted reception, interrupted marker commit, interrupted installer copy, and an installed but unbootable application. A simulator model of one does not demonstrate another.
R3 needs recovery through an iPhone when the application is unusable, complete official-image validation, installer/configuration preservation, and survival of interrupted copying. A sacrificial device is a test resource, not evidence that these properties hold.

## 9. Per-run record

Create a private record before a future authorized run. Retain failed runs as well as successful ones:

```text
Run ID / date / operator:
Separate authorization reference and allowed operations:
Test-unit identifier (private), exact model/hardware, original firmware:
Primary keyboard excluded from the station:
Candidate source commit / CI runs / .bin SHA-256 / ELF/map hashes:
Phone/app/toolchain versions:
Original preservation inventory and hashes, missing regions:
Restoration route and prior evidence:
Approved installation procedure and timeout/stop policy:
Each state from section 7, outcome and evidence path:
Typing/layer/profile/charging observations:
Unexpected events, partial outcomes, and stop reason:
Final unit state and official-restoration evidence:
R3 status (unchanged unless separately reviewed):
```

This M3 deliverable ends at a reviewed procedure and software artifacts. Acquisition, physical compatibility, and restoration remain blocked on separate decisions and evidence.
