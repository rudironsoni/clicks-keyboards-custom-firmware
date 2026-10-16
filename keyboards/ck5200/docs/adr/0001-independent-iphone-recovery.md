# Independent iPhone recovery is required before any custom flash

Decision: **NO-GO for custom flashing. R3 remains UNMET.** Date: 2026-10-15.
Before editing, local `HEAD` and remote `main` both matched `5f87e3295445fbd4b4f09a9217fd83016b20efc7`.
The accepted stock diagnostics baseline stays closed. No independent recovery service is demonstrated.
The patched-stock reader cannot establish safety before its own installation, so deployment is rejected.

## 1. Evidence and the execution boundary

VERIFIED below means a checked artifact, source, or documented hardware rule. It does not mean physical recovery was tested.
[INFERENCE] marks conclusions from those facts. UNKNOWN means evidence is absent.

| Requirement or boot behavior | Evidence | Conclusion |
| --- | --- | --- |
| Reset entry and application handoff | V122 contains an application reset jump at `0x2000` to `0x485C`. Startup initializes `gp`, stack, RAM, and `mtvec` at `0x485C–0x48CA`. Neither archived image contains addresses below `0x2000`. [E1] | VERIFIED application entry. [INFERENCE] Earlier code must select this application entry during normal startup. Its reset path, handoff checks, and fallback are UNKNOWN. |
| Execution independent of QMK | Stock dispatcher `0x3216` and its callback `0x2208–0x2218` reside in the application. Custom EP2/EP3 dispatch also resides in application callbacks. [E1] | VERIFIED dependency. Neither demonstrated update path survives an application that cannot execute. |
| Entry after erased pages or corrupt vectors | No original boot-stage code or applicable vendor recovery procedure was found. The application handles the examined key combination. [E1,E2] | UNKNOWN validity checks, recovery triggers, and behavior after invalid vectors or erased application pages. A reset alone does not establish recovery entry. |
| iPhone transport without the application | Stock USB descriptors, iAP2 handling, and authentication calls exist in the downloaded application. The accepted session used `com.clickscompanion.protocol`. [E1,E3] | VERIFIED working-stock transport only. Boot-stage enumeration, authentication, protocol advertisement, and custom-app access are UNKNOWN. R3 is UNMET. |
| Accept a complete, validated official image | Archived V122 is 18,648 bytes, SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`. Application A1/A2/A3 stage bytes and a length marker. [E1] | VERIFIED image identity and staging implementation. Boot-stage length, integrity, signature, model checks, and recovery acceptance are UNKNOWN. |
| Preserve installer and device configuration | The custom linker limits application bytes to `0x2000–0x89FF`. Its staging guard limits page writes to `0x08008A00–0x0800F4FF`. Serial/configuration reads use `0x0800F600`/`0x0800F700`. [E1] | VERIFIED limits of these code paths. They do not prove boot-stage write bounds or hardware protection. Full device-specific storage coverage remains UNKNOWN. |
| Survive power interruption | A3 commits the length at `0x08008A00` through a page transaction. The host harness models partial marker programming but never executes the installer. [E1] | UNKNOWN marker validation, copy order, retry state, marker clearing, rollback, and restart after interrupted copying. No persistent recovery service is proven. |
| No recovery-time pads, programmer, or adapter | No inspected source demonstrates a qualifying entry and transport. [E1–E3] | UNMET. Hardware readback would be forensic evidence, not this product's recovery mechanism. |

**No immutable recovery boundary is established.** The reserved first 8 KiB are a working layout assumption, not a verified fail-safe bootloader.
WCH documents main flash at `0x08000000–0x0800F7FF`, separately from its factory launcher at `0x1FFF0000–0x1FFF0CFF`. [E4]
The presumed Clicks installer occupies main flash. It is not the factory launcher.
Read protection automatically write-protects only pages 0–7, or 2 KiB. Other write protection uses 2 KiB units. [E4]
The keyboard's option bytes, protection registers, and boot-stage implementation remain unread.
Thus neither the full 8 KiB's immutability nor its survival during faulty copying is proven.

## 2. Recovery architectures

These are architectural assessments, not available solutions.

| Architecture | Assessment and unresolved requirements |
| --- | --- |
| Existing Clicks boot stage contains independent iPhone rescue | [INFERENCE] The only identified candidate that could meet all requirements without replacing the original boot stage. Needs evidence of entry after application failure, its own USB/iAP2/authentication service, complete-image validation, bounded copying, and restart after power loss. None is established. |
| Existing installer retries a retained staged stock image | [INFERENCE] Could address interrupted copying if it validates and retains the source until completion. Does not alone meet R3: after a complete but unbootable custom installation, it still needs an independent trigger and an iPhone path for stock delivery. These behaviors are UNKNOWN. |
| Application-level rescue loop | Feasible to develop offline using the existing USB/iAP2/update components. It requires a valid reset path, RAM, clocks, interrupts, and transport initialization. It cannot recover erased application code, corrupt entry vectors, or faults before rescue entry. It does not meet R3. |

A replacement bootloader or protected new rescue partition would require changing the boot arrangement before recovery exists.
Neither is an acceptable bootstrap on this keyboard. No original-installer replacement is proposed.
Factory ISP, generic DFU, and a USB vendor interface do not establish an iPhone transport. No ISP experiment is proposed.

Apple's `EASession` requires an available `EAAccessory` and a protocol that it recognizes. [E3]
`UISupportedExternalAccessoryProtocols` declares protocols in `Info.plist`. It is not permission to access arbitrary USB endpoints.
ExternalAccessory and this key are available from iOS 3.0. The accepted stock session confirms availability on this iPhone.
USBDriverKit explicitly supports macOS and M-series iPads, with iPad driver support from iPadOS 16.
The `com.apple.developer.driverkit.communicates-with-drivers` entitlement permits app-to-driver communication on supported hardware.
Its iOS availability badge does not override the documented hardware limit. The USB transport entitlement also does not add iPhone support.
IOUSBHost documents macOS and Mac Catalyst availability, not an iPhone API. [E3]
No supported public iPhone route to an original recovery service was found. This does not prove that an undocumented service cannot exist.

## 3. Readback and the patched-stock proposal

**Installer readback is useful, not sufficient, and not logically necessary.**
A matching vendor implementation or complete documented recovery contract could supply the missing evidence without reading this keyboard.
A dump could reveal triggers, validity checks, copy behavior, descriptors, and authentication dependencies.
It would still need matching configuration evidence and validation of entry, iPhone communication, preservation, and interruption behavior.
A dump alone neither provides an executable recovery path nor proves those properties.

Offline patch assessment, using the hash-verified V122:

| Question | Finding |
| --- | --- |
| CPU access below the application | [INFERENCE] Byte loads from physical main-flash addresses `0x08000000–0x08001FFF` are plausible. Stock already reads flash through CPU loads. The manual describes memory-mapped flash but does not establish access under this keyboard's flash protection or physical memory protection (PMP) configuration. Readback on this unit remains UNVERIFIED. [E1,E4] |
| Space and insertion point | VERIFIED V122 ends at `0x68D7`. There are `0x2128` bytes between `0x68D8` and the application's exclusive limit `0x8A00`. This is layout capacity, not proven installer acceptance. A candidate could redirect the command-08 handler at `0x335E` to appended code without moving existing addresses. No insertion point is certified safe. |
| Initialization, interrupts, and protocol | The candidate would retain stock startup, vectors, USB, iAP2, and authentication. It would run only after the application and accessory session work. It must preserve registers, stack, response bounds, callback lifetimes, and interrupt behavior. A read operation would not erase/program flash, but that does not make its installation safe. |
| Binary integrity and relocation | Startup copies initialized data from `0x6880`. Appending code could preserve existing data and PC-relative references. Replacing instructions needs correct RISC-V/WCH decoding and branch ranges. Host structural validation cannot prove instruction correctness, hidden checksums, proprietary acceptance, or boot behavior. |
| Protection | The manual warns that releasing read protection erases the user area. Its protection section does not prove that this proposed CPU read works in the actual configuration. No protection change or bypass is permitted. [E4] |
| Value of the result | A successful dump would reduce boot-stage uncertainty. It would not prove independent iPhone recovery. A rejected patch or startup fault could remove the only demonstrated communication path. |

**No reader binary was built or installed.** No image manifest or iOS bundle was changed.
The current iOS authorization also rejects a modified stock image. That restriction stays in place.
Stop the reader investigation before any test requiring replacement of the stock application without independent recovery.
Any future offline artifact must remain explicitly non-deployable and outside authorized manifests and installable bundles.

## 4. Smallest evidence that could change the assessment

A model-matched vendor recovery entry specification is the smallest useful addition:
it must state how CK-5200 hardware `1.2.0` enters recovery with invalid application code and which iPhone-accessible protocol it exposes.
That would change the assessment from an unknown service to a concrete implementation to inspect. It would not alone permit flashing.
GO still requires evidence for complete stock validation, installer/configuration preservation, and recovery-service survival during interrupted copying.

The existing official packages and reviewed public implementations did not supply that evidence. [E1,E2]
The stock download URL was already known. HTTPS inspection was stopped and is not a next task.
No vendor message was sent. No SDI operation is authorized.
A one-time SDI read could be considered only under separate authorization after physical and protection risks are understood.
It would remain evidence acquisition, not an accepted recovery architecture.

## 5. Next safe implementation task

Develop a **host-tested application-level rescue mode**, reusing the pinned QMK, TinyUSB, iAP2, and staging components.
The current QMK `main()` calls `keyboard_setup()` and `keyboard_init()` before its service loop.
An offline rescue branch would need to bypass those calls and `keyboard_task()`, while keeping USB/iAP2 processing available.
Limit the change to that application boundary. Do not claim coverage for corrupt vectors, failed low-level startup, or erased application pages.
Acceptance: a host test reaches the authenticated update service while keyboard initialization/task hooks deliberately fail if called.
Keep diagnostics and image authorization gates closed. This task does not resolve R3 or authorize deployment.

Existing offline checks passed on 2026-10-15: stock image hashes/structure, 15 iAP2 session cases, four authentication cases,
five USB protocol tests, and the staging/fault harness. These checks do not execute the original installer.
No physical test, reboot experiment, firmware write, ISP entry, protection change, or debug-pad operation ran.

## Evidence index

- **E1:** [Recovery audit](../recovery-audit-2026-10-14.md), including artifact hashes, linked addresses, source paths, and host-test limits.
- **E2:** [Public-source search](../public-recovery-search-2026-10-14.md), including vendor guides, ClicksFT history, and the unrelated WCH IAP example.
- **E3:** [Apple transport evidence](../iphone-recovery-transport.md), including APIs, OS availability, hardware limits, and entitlements.
- **E4:** [Pinned WCH manual evidence](../recovery-audit-2026-10-14.md#hardware-boundary-evidence-2026-10-15), sections 1.2 and 20.1–20.2.2.
- Implementation boundary: [pinned QMK main](https://github.com/qmk/qmk_firmware/blob/3d4da6de29c8635c9cd232ce456d8fec8d31921b/quantum/main.c), [protocol service](../../firmware/platform/ch32x035/protocol.c), and [staging writer](../../firmware/platform/ch32x035/ck5200_staging.c).
- Authorization remains owned by [flash-readiness.md](../flash-readiness.md).

## 2026-10-16 extension: external adapter research is in scope

The [USB recovery feasibility report](../../../../reports/CK5200%20USB%20recovery%20feasibility.md) extends this decision through research and offline models.
An iPhone-controlled BLE adapter with its own USB host is now within conceptual scope.
This supersedes the no-adapter architecture constraint in section 1 for this investigation.
It does not authorize hardware construction, purchase, keyboard access, reboot, probing, ISP, or protection changes.
There is one keyboard and no spare-purchase plan.

**PROVEN (chip documentation):** PC17/D+ high at power-up selects BOOT.
**SUPPORTED HYPOTHESIS [INFERENCE]:** the inner male USB-C plug could provide independent entry if the early data and power paths permit it.
**BLOCKED [UNVERIFIED]:** the actual connector routes, VDD timing, safe bias window, retained loader, and protection state remain unknown.
WCH System FLASH is separate from the presumed Clicks installer, but it is mutable through supported debug tooling. It is not immutable ROM.

The inspected `wchisp` programs from zero and provides no proven application-range erase.
A6 compares supplied bytes rather than reading unknown main flash.
The final stock page includes 40 unknown bytes outside the image. Their preservation remains required.
Thus successful entry or enumeration would not establish a permissible restore or power-loss recovery.
The report records exact source pins, a conditional circuit, alternatives, and eight falsifiable R3 criteria.

**Section 5's next-M2 task is superseded.** M1 and M2 are closed in their accepted scopes.
M3 is closed for the software scope of this investigation, with its existing review and CI evidence retained in
[stock-manager-readiness.md](../stock-manager-readiness.md). This does not claim physical installation or independent recovery.
The next step is model-matched vendor/source evidence and offline rejection of unsafe assumptions, not another application rescue implementation.
The vendor inquiry remains unsent. The [offline lab](../../recovery_lab/README.md) passed 10 unit tests on local rerun.
Its default CLI returned expected exit 2 (blocked). The hypothetical CLI returned 0 for an image/region model match.
The model requires `tail_preserving_erase` before any mutation. This stronger hypothetical contract is not known CH32 page erase or a factory opcode.
The restart fixture requires the original in-memory journal. No durable host journal or OEM normal-boot proof exists.
A1 identity fixtures, A4 count decoding, A5 packet decoding, and A6 comparison fixtures exist. A3/A7 are not implemented.
The final ISP/lab review closed all four findings with no actionable blockers. The electrical review found no P1/P2 blockers for conceptual scope.
The [review summary](../../../../research_notes/CK5200%20USB%20recovery%20feasibility/review.md) records the reported checks and their limits. Neither review establishes physical recovery.
**R3 remains UNMET and custom flashing remains NO-GO.** [flash-readiness.md](../flash-readiness.md) remains the sole readiness authority.
