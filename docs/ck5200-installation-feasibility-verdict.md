# CK-5200 installation feasibility verdict

Decision: **NO-GO for physical installation under the current constraints.**

Date: 2026-10-23. Investigation baseline: `65198ce8df5204352ffdc6dc7d67599e3dc18528`.
This is not a claim that custom firmware is technically impossible.
The working, fully programmable physical keyboard was **not delivered**.
Normal typing still works, as reported by Rudi.

## Decision gates

| Required gate | Result |
| --- | --- |
| Original iOS material behavior | Encoder, raw-image path, model routing, worker and stream lifecycle reconstructed. Runtime/library limits remain explicit in the [iOS record](ck5200-original-ios-investigation.md). |
| Exact custom executable | CRT and extension initialization execute. Both captured commands return `CKX1` through the original receive adapter in bounded fixtures. Genuine startup stops at unsupported RCC access. Full gate remains BLOCKED. |
| OEM installation understood | Application staging/marker/reset-request contract established. Boot-stage acceptance, copying and interruption handling remain unknown. |
| Viable installation demonstrated | NO. The physical transaction demonstrates acknowledged transfer, not installation. |
| Independent stock recovery | NOT DEMONSTRATED. Application-level rescue depends on a working application and does not satisfy R3. |
| No critical incompatibility | NOT ESTABLISHED. Copier length/format and physical startup remain unverified. |

Any unmet installation or recovery gate requires NO-GO. More host tests cannot
substitute for these gates.

## Findings

1. The decrypted iOS encoder is recoverable. It emits the same A1, A2, A3 and A0
   bytes as all **851** preserved update requests, including all 848 A2 chunks.
   Android parity is separate supporting evidence, not the basis of this result.
2. The vendor EAF path reads downloaded bytes, checks SHA-256, and sends raw
   bytes. It does not add the 78 trailing `FF` bytes that our installer added.
   This is a real difference. It is **not a demonstrated cause** of rejection.
3. The vendor writer ignores short-write counts. Its A1/A3 checks ignore status
   and size payloads. Its worker reports success even when A0 fails or times out.
   Our installer is stricter and preserves uncertainty. Copying those vendor
   weaknesses would not fix the failed installation.
4. Stock V122 can acknowledge progress without independently checking programmed
   bytes. No retained device readback closes that gap.
5. Full startup reaches the RCC CTLR read at `0x3BBE`, address `0x40021000`, in
   stock, exact raw and exact padded images. The current runner rejects this
   unsupported access. It does not prove a physical startup defect.
6. Removing the command hook reproduces both exact physical `FF` replies.
   This establishes one possible mechanism. It does not establish which image
   or flash bytes actually execute on the keyboard.

No demonstrated defect in our firmware or installer explains the physical
failure. No firmware or installation-byte correction is justified by these
results. The firmware artifact remains unchanged. Narrow changes add offline
forensic checks and runner assertions only.

## Physical root cause

**Undecidable from retained evidence.** The records cannot distinguish staging
or marker failure, reset not completing, copier rejection/skipped/interrupted
copying, a different active image, or damaged/bypassed extension startup/dispatch.
The [fault tree](ck5200-installation-fault-tree.md) gives each causal chain and
the missing observation. No unsupported ranking or confidence percentage applies.

The iOS application contains client code, not the keyboard's boot-stage bytes.
The firmware downloads are application images linked at `0x2000`. Neither source
can reveal the actual contents or execution of the unobserved device regions.

## Minimum evidence that could change this decision

All of the following evidence is necessary. These are conditions, not a new
development roadmap or authorization for a physical trial.

| Evidence | What it would resolve | Constraint impact |
| --- | --- | --- |
| Provenance-checked CK-5200 `[0x0000,0x2000)` bytes or matching OEM copier evidence, plus its format/length/error contract | Acceptance and copying of this exact image, not merely A1 acceptance | An existing independently supplied artifact could be analyzed offline. Reading it from this keyboard or contacting Clicks violates current constraints. |
| Trusted staging bytes, marker, destination bytes and completed-reset/copy observation for the failed transaction | Distinguishes the remaining physical causes | Existing retained evidence would be allowed, but none was found. New keyboard commands, probing or reset experiments violate current constraints. |
| Exact-image startup evidence through peripheral/USB/authentication and original command dispatch | Closes the RCC and later hardware boundaries | Model evidence supplied independently can be inspected offline. New physical validation requires changed authorization and cannot alone close installation/recovery. |
| Independently reliable iPhone-only stock recovery from an unbootable application | Closes R3 without relying on the code being replaced | Existing model-matched evidence is allowed. A new physical recovery trial violates current constraints. External programmers and hardware changes remain excluded. |

A boot-stage dump alone is insufficient. An emulator pass alone is insufficient.
A further acknowledged upload is insufficient. Do not weaken the recovery gate.

## Evidence and preservation

- [Original iOS application](ck5200-original-ios-investigation.md).
- [Five-way differential](ck5200-updater-differential.md).
- [Executable, OEM and recovery evidence](ck5200-executable-and-oem-evidence.md).
- [Causal fault tree](ck5200-installation-fault-tree.md).
- [Sanitized evidence ledger](evidence/ck5200-installation-feasibility-20261023.json).

The ledger separates observed bytes, static executable facts, reconstructed
behavior, emulation and unknowns. Reproduction requires the retained private
artifacts. They are not published.

Verification before publication: 37 existing Python tests, the full Swift
suite, native harnesses, 20 bounded assurance cases, six prior forensic cases,
nine receive controls, three startup-boundary controls, 18 reconstructed iOS
worker cases, seven selection/validation assertions and all 851 captured frames
passed. Debug and ExtensionInstaller Simulator workflows passed locally.
Readiness, input hashes, changed-document links, whitespace and focused secret
checks are required before commit. Hosted results must refer to the published
commit, not the baseline runs. The final response records their actual status.

Skipped or blocked: execution of the original app under its iOS OS runtime,
peripheral startup past the RCC boundary, physical installation and physical
recovery. No test result in this investigation closes those gates.

No physical keyboard command, firmware write, reset, power-cycle experiment,
reconnect experiment, phone app replacement/launch or journal change occurred.
`sentRebootPending` remains unchanged. M4 remains closed. R3 remains unmet.
