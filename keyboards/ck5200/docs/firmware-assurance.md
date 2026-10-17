# CK-5200 binary-execution assurance

Date: 2026-10-17. This record covers the first software-only execution gate.
[flash-readiness.md](flash-readiness.md) remains the only authorization authority.
**R3 remains UNMET. Physical custom flashing remains NO-GO.**

## Proposal review before implementation

The independent adversarial review accepted a bounded application-startup gate.
It rejected using an unchanged CHGame emulator as assurance evidence.
The review checked the prepared candidate and the pinned emulator source, without executing either or accessing a device.

Candidate under review:

- BIN: 26,496 bytes, SHA-256 `94ba2f708b69a754f26698cad700d0969601322c6bb692a38c8533cb60e3d2e4`.
- ELF: SHA-256 `0ad0afccae603914cdf90e9298ef3da2f3e0a62c6ea7e877a730ba0d7dd1d298`.
- Source context: the existing uncommitted [QMK delivery](qmk-delivery-evidence.md) on `feat/software-only-qmk-delivery`, based on `497b8e1592344567f1f5452443050b2fa692c814`.
- Emulator: [`joyrider3774/chgame_emulator` at `bb183d7863961b5f53dab3c8d3d3c7a9dc4b1a48`](https://github.com/joyrider3774/chgame_emulator/tree/bb183d7863961b5f53dab3c8d3d3c7a9dc4b1a48), MIT licensed.

### Findings that determine the gate design

All emulator source locators below use that fixed commit.

| Severity | Finding | Required correction |
| --- | --- | --- |
| P1 | `src/bus.c:1093–1102,1145–1146,1191–1196` silently stores unknown peripheral accesses, returns zero, or discards writes. | Check register address, access width, direction, and supported semantics before effects. Reject unknown accesses. |
| P1 | `src/rv32.c:627–670,903–916` aliases unknown CSRs and ignores the validity result. Fast memory paths bypass the bus. Some exceptions do not set `faulted`. Opcode `0x0f` decoding is too permissive. | Stop on unsupported CSR/instruction, unexpected exception, invalid execution/memory range, or reset. Check cached and direct access paths. |
| P1 | `src/loader.c:183–227,275–300` uses CHGame's `0x3000` application layout and manufactures boot metadata at `0xF700`. | Load the exact CK-5200 BIN at `0x2000`. Do not use CHGame's loader, embedded bootloader, or save machinery. |
| P1 | `src/machine.c:25–42` imposes CHGame reset flags and state. The stock OEM handoff remains unknown. | Name synthetic reset profiles. Test retained RAM separately. Claim direct application entry, not OEM boot. |
| P1 | USB in `src/bus.c` is byte storage. It does not implement interrupt-flag write-one-to-clear behavior or host enumeration. | Model only the documented disconnected-device register behavior needed by startup. Do not manufacture enumeration or authentication success. |
| P1 | Upstream headless execution can return success after a fault. Retired instructions and reaching `main` do not establish useful progress. | Require ordered startup checks, completed QMK work, SysTick entry/return, and foreground progress within a fixed bound. |
| P2 | Upstream flash operations replace a page before advancing modeled time. Protection values are hardcoded. | Reject flash writes and unlock/erase/program commands in this first gate. Do not claim interrupted-update assurance. |
| P2 | The firmware build did not pin or validate the emulator dependency. Instrumented firmware would not be the release artifact. | Pin the host dependency, validate BIN/ELF correspondence, and run the exact artifact that CI uploads. |

The review also found an upstream chip-label mismatch: `0x03510601` is labeled G8U6 in the emulator.
The pinned WCH GPIO implementation maps the masked identifier to C8T.
The keyboard's C8T package remains an inference, so a synthetic identity is a model input, not a new physical observation.

### Required first-gate claims

1. Guest startup initializes `.data`, `.bss`, `gp`, and `sp` from the matching ELF while preserving `.noinit`.
2. Normal entry reaches QMK and completes bounded repeated scans and SysTick interrupt returns.
3. Warm entry with a valid rescue request follows the transport loop and consumes the request without entering QMK.
4. An invalid rescue request follows normal startup. Warm-state cases retain modeled RAM.
5. Unknown operations, forbidden writes, invalid code, unexpected resets, and missing progress produce failure, not a partial pass.
6. Negative controls demonstrate rejection of the wrong-family failure class and CPU/model contract violations.

These are bounded observations under explicit model assumptions. They are not exhaustive liveness or memory-safety proofs.

### Corrections to the broader proposal

- A finite execution trace proves that particular modeled execution, not every future execution or indefinite runtime progress.
- Instruction fixtures check selected CPU behavior. They do not certify the complete emulator against the physical MCU.
- Reset profiles must state initial interrupt/controller state. In the pinned startup, interrupt enable precedes installation of the vector table.
- A successful direct entry at `0x2000` does not prove the OEM installer supplies that entry or the same initial state.
- BIN/ELF agreement establishes the loadable image, including initialized data. It does not mean every image byte is an executable instruction.
- A disconnected USB model can establish bounded software startup while no host responds. It cannot establish enumeration or completion recovery.
- CBMC bounds, Frama-C proof obligations, and SPIN fairness contracts require separate artifacts. None can be inferred from an emulator pass.

## Boundaries retained from the proposal

- No original Clicks installer executes. Starting at `0x2000` assumes the missing handoff.
- The first gate does not establish USB enumeration, MFi authentication, HID delivery, or physical matrix wiring.
- The first gate checks the firmware artifact uploaded by firmware CI. It does not add a QMK image to the iPhone app or validate a new phone package.
- Historical note: the original binary profile preceded the liveness fixes. Host tests now cover debounce starvation and one lost-HID-callback recovery; progress-aware watchdog, connected-USB emulation, and formal proofs remain separate.
- A watchdog can reset a stalled executable application. It cannot repair invalid flash or establish independent recovery.
- The candidate spans the unknown stock tail `[0x68D8,0x6900)`. Executing it in synthetic memory does not authorize overwriting that region.
- The plausible-marker/partial-payload installation hazard remains unresolved. No successful startup model closes it.
- No purchase, vendor contact, physical reset, ISP command, flash write, or keyboard access belongs to this gate.

## Implementation and verification

The [strict runner](../assurance/README.md) executes the exact BIN using a locally patched copy of the pinned emulator.
It checks BIN/ELF identity before execution and records hashes after execution.
The candidate firmware, build artifacts, and upstream dependency checkouts remain unchanged.

Run from `keyboards/ck5200`:

```sh
python3 assurance/run.py --image build/ck5200_qmk.bin --elf build/ck5200_qmk.elf
```

Local ASan/UBSan verification passed:

| Case | Completed scans and task returns | Rescue loops | SysTick entries and validated returns |
| --- | --- | --- | --- |
| Cold, nonzero seeded RAM | 1,364 | 0 | 99 |
| Warm, retained RAM | 1,364 | 0 | 99 |
| Warm, valid rescue request | 0 | 6,227 | 99 |
| Warm, invalid request word | 1,364 | 0 | 99 |

Each case runs for 4,800,000 modeled cycles, nominally 100 ms at 48 MHz.
A final instruction can cross that boundary.
The model starts with no enabled or pending PFIC interrupts and disabled SysTick.
These initial conditions do not establish the OEM handoff state.

Controls passed for 193 XW encodings, each decoded upstream and executed with and without cached decoding.
Further controls passed for 21 rejected operations, six execution boundaries, two initial stalls, and two late stalls.
Artifact controls now accept compiler-comment differences and reject five image or runtime-contract changes.
The local report is `keyboards/ck5200/build/assurance/report.json`.
Its adjacent JSONL files contain ordered execution traces.

### Implementation review and correction

The independent implementation review found one P2 defect in the initial progress check.
Early completed loops could satisfy cumulative counts even if execution stopped for the rest of the observation window.
The reviewer reproduced false passes by stopping execution after about 50 modeled ms, including inside a SysTick handler.
This was a gate defect, not a demonstrated firmware hang.

The corrected gate requires a validated foreground completion and SysTick return within separate 480,000-cycle maximum gaps.
Both deadlines include startup and the final observation boundary.
Checks run before completion timestamps can update, so late completion cannot hide a missed deadline.
Scheduling slices also stop at the next deadline.

The late-foreground control now fails with `foreground-progress-timeout` while SysTick returns continue.
The late-handler control now fails with `SysTick-progress-timeout`.
Both preserve the exact flash bytes.
The reviewer reran the original counterexamples independently under ASan/UBSan and closed the finding with no remaining blocker.

An independent integration review checked the workflow, dependency pin, bootstrap addition, and authorization boundary.
It found no blocker in those files. CI uses the existing build, preserves runner failure through `pipefail`, and uploads the assurance evidence separately.
The workflow also requires a nonempty `report.json`, so a log alone cannot satisfy the evidence requirement.
The runner arguments and output paths match the workflow.
CI retains the JSON report, JSONL traces, and log before any successful firmware upload.
### Hosted ELF failure and correction

[Run 39050140084](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39050140084) passed the build and firmware tests, then failed the whole-ELF identity check.
The matching BIN did not make that gate failure acceptable.
Diagnostic [run 39051670366](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39051670366) retained the actual inputs and reproduced the same ELF hash:
`ef8df7d3bbf94e5518c7c64e18fd5ce7bf66600c23011e8a28eda84138d4d7db`.

The section comparison established the cause: `.comment` names the compiler host as `x86_64` instead of `arm64`.
Its extra byte shifts the file offset of `.riscv.attributes` by one.
The CPU attributes, symbols, entry point, load segments, and firmware bytes match the reviewed local artifact.

The corrected gate pins the exact BIN and an ELF contract derived from the original reviewed local ELF.
It hashes all section and segment contents and runtime descriptors, excluding compiler-comment text and file placement.
It retains the entry, vector, symbol, ABI, permissions, complete loadable-image, and tracepoint checks.
The raw input hashes remain in the report.
Changing the expected raw ELF hash to the CI value is not the fix.

Both actual local and hosted inputs passed the corrected gate locally under ASan/UBSan.
The compiler-comment regression failed before the correction and passes afterward.
Runtime symbol, segment-permission, entry, ABI, and BIN changes still fail before execution.
Independent review verified the section-level cause and found no runtime-contract normalization bypass.
[Hosted run 39052293311](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39052293311) passed at `3ae82074aa4df310b7310533d7856e879172b05a`.
The build, existing firmware tests, strict gate, final inspection, and firmware artifact upload all succeeded.
Its report confirms ASan/UBSan, four passing startup cases, the pinned contract hash, and all rejection controls.

The diagnostic workflow passed the existing firmware suite with the unchanged candidate.
No physical tests ran. R3 remains UNMET and physical flashing remains NO-GO.

## Runtime-liveness gate (2026-10-17)

This section adds results for the **new candidate**, not a retrospective change
to the first-gate evidence above. R3 remains UNMET; physical flashing NO-GO.

### Red-first and implemented fix

- Branch: `feat/ck5200-runtime-liveness`, based on verified `6a29277`.
- Red-first commit `9af647f`, then standalone dual-regression runner `1456de8`.
  [Red CI run 39065223970](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39065223970)
  proved both failures against the original production paths:
  `sym_defer_g` suppressed a stable `KC_M` under independent chatter;
  a lost HID completion froze matrix scanning (`delta=0`) and blocked
  the pending release even after the endpoint became ready.
- Fix commit `90c29cc`: static 6x6 per-key deferred debounce without
  allocation; scanning is independent of HID backpressure; the in-flight
  queue head is retained during a callback loss and is only resubmitted
  after the endpoint reports ready and a 20-millisecond grace window.
  A busy endpoint is never resubmitted into. The queue preserves its
  bounded-overload policy; no change was made to rescue/installer writes.
- [Host regression CI 39068934989](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39068934989)
  passed both new checks and the prior host suite. Its exact-binary
  gate correctly rejected the new artifact under the old hash.

### New exact-binary execution profile and evidence

The strict profile was deliberately re-reviewed using the compiled artifact,
not automatically widened to accept any build. The [passing hosted gate
39069270426](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39069270426)
produced this evidence:

| Property | Verified result |
| --- | --- |
| New BIN | 26,644 bytes; SHA-256 `4f30569f0e439137e18899dbec268eacfee0af54f7e55334fb82b86e6842f394` |
| Hosted ELF | SHA-256 `362efbe227ed3a0a908659efdec4acdd7fc5eaa1d85d6e1beb2342aa6a85d714` |
| Strict ELF runtime contract | `0d0a7a722563f5c15ea927d24241d61bca3652e647fa5918d9987b5001f1b8c2` |
| Application flash | `[0x2000,0x8814)` inside `[0x2000,0x8a00)`; only 492 bytes remain |
| RAM sections | 144-byte initialized data, 4,680-byte BSS; stack budget unchanged |
| Normal cases | Cold, warm-retained, and warm-invalid rescue: 913 complete scans and normal loops each, 99 validated SysTick returns |
| Valid rescue | 6,227 transport loops, zero QMK scans, 99 validated SysTick returns |
| Model and failure tests | 193 XW fixtures; 21 rejected operations, six execution bounds, two initial and two late-stall controls |
| Artifact controls | Compiler metadata change accepted; BIN mismatch and mutated runtime symbol, ABI, entry, or segment permissions rejected |

Tracepoint meanings and disassembly were checked individually:
`SystemInit` return `0x8006`, startup MRET `0x8012`, rescue consumption
`0x7bcc`, normal-loop head `0x7ea6`; disconnected USB chooses the
`keyboard_task` call at `0x7ef6` with return `0x7efa`, then normal
loop-back at `0x7f4a`. Matrix GPIO read `0x6118` indexes the row in
`a5` (x15) and completes all six rows at `0x6134`.
SysTick MRET is `0x7808`; rescue loop `0x7c8a` to `0x7caa`.
All PCs and register choices are artifact-specific, and changing the BIN
requires re-review of this contract.

### Claims explicitly excluded

- The model's USB remains **disconnected**, so it does not test actual
  USB enumeration, iAP2/MFi, real endpoint behavior, or on-device typing.
  The host protocol test exercises a scripted callback loss but not every
  interrupt ordering or a permanently busy USB endpoint.
- Watchdog supervision is absent; progress in this 100-ms modeled
  execution is not a proof of perpetual forward progress.
- The model starts directly at application address `0x2000`, not through
  the installed OEM boot stage. It cannot prove actual installer behavior,
  protected tail preservation, or recovery from interrupted flashing.
- No physical device operation, software flashing, hardware modification,
  peripheral accessory, or vendor communication occurred.
  **R3 is UNMET. Physical flashing remains NO-GO.**
