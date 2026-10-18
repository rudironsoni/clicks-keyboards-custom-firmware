# FIRST: CK5200 exact-binary execution gate

From `keyboards/ck5200`:

```sh
python3 assurance/run.py --image build/ck5200_qmk.bin --elf build/ck5200_qmk.elf
```

Requires Python 3, a host C compiler with ASan/UBSan, `patch`, Git, the existing
RISC-V binutils, and clean pinned `external/chgame_emulator` and `external/ch32x035`
checkouts. No SDL, GUI, device, network, bootloader loader or firmware build runs.
`--no-sanitize` explicitly skips host sanitizers and records that choice.

Output: `build/assurance/report.json` and four ordered `*.trace.jsonl` files.
`--report PATH` changes the JSON report path. Any missing obligation, unsupported
model access, guest exception, input mismatch or host timeout returns nonzero.
The scratch executables are internal test tools. Only `run.py` validates the
artifact identity and is an assurance entry point.

## Release binding

This profile accepts only the reviewed release:

- BIN: `9fb5af9d3c474ea8df761857bfb5fb678231e90de22da5b8c861af1cc9b44439`, 26792 bytes.
- ELF contract: `3348f035f6e7d1fa319159c6aaadd2c8669082df0ec8ba9e1531497cd650785b`.
- Hosted verification for this watchdog candidate is pending.
- Local ELF provenance: `5a016bdfac12e0d1fd4c1d2879b07cd7604dfcd176c720ec0abccd4c3ccf0c24`.
- Application bytes end at `0x88a8`, leaving 344 bytes before `0x8a00`.
- CHGame: `bb183d7863961b5f53dab3c8d3d3c7a9dc4b1a48`.
- OpenWCH register reference: `c6253942b29ff2a3f5e2829182a874afffa45881`.

The existing `tools/validate_image.py` first checks BIN/ELF byte identity and
layout. Symbols and `.text` bounds come from that ELF. Internal LTO tracepoints
in `run.py` are bound to the BIN and ELF contract hashes and checked against disassembled bytes.
**A changed runtime artifact requires a reviewed profile.** Merely building a new artifact
cannot make this gate pass. Do not update the hashes without reviewing the
tracepoint meanings and the executed register/instruction paths.

The ELF contract includes header identity, CPU/ABI flags, entry point, section
descriptors and contents, and program-segment descriptors and contents.
This includes the complete symbol table, vectors, initialized data, RAM layout,
CPU attributes, segment permissions, and every loadable byte.
It excludes file offsets, unused file padding, and non-allocated `.comment`
text and length. These do not affect the reviewed executable.
The actual raw ELF hash remains in each report for provenance.

Hosted run `39050140084` failed the original whole-ELF hash check.
Diagnostic run `39051670366` retained the exact same hosted ELF for comparison.
Its compiler comment says `x86_64` instead of `arm64`, moving the attribute-section
file offset by one byte. Runtime symbols, attributes, load segments, and BIN match.
Both historical artifacts shared an earlier profile contract. The consumer
recovery correction has a new exact-BIN profile, reviewed against the rebuilt
baseline and corrected ELF disassembly before updating any identity.
The consumer setter stores the new usage then jumps through the existing
`host_keyboard_send` at `0x266a`. The same FIFO owns consumer and keyboard states.
The watchdog candidate calls QMK at `0x8020`, returns at `0x8024`, and
branches from `0x8084` to loop head `0x7fcc` in disconnected execution.
GPIO row read `0x614c` uses `a5` (x15), with the six-row bound at `0x6164`.
Scan completion `0x6176` follows the final row-drive reset at `0x6174`.
`SystemInit` returns at `0x7c78`, startup MRET is `0x7c84`, and SysTick MRET
is `0x7848`. Rescue consumption is `0x7cda`, transport return is `0x7de4`,
and the rescue loop spans `0x7d98` to `0x7dce`. Watchdog servicing at `0x2e0e`
reads and consumes the progress byte at `0x20000120`, checks the required marks
and a new tick, then writes the reload key at `0x2e38`. The initial enable store
is `0x7be0`, before the first poll and before GP/SP, data/BSS, or SystemInit.
The reset path uses the existing `_einit` vector base and the linker retains
`.vector` with `KEEP`, preserving its `0x2004` address and `0xfc` size. These anchors were reviewed in the
new disassembly before profile replacement.
The preceding consumer-corrected BIN was
`322f227997d5ea8c291a06dc2e5c210be31f1a97c00816c18ffcf161e7326c00`,
with contract `de296b6d1ed8b342e07c2426f51d672f3d34990104ec340d2d73e42cc31296d2`.
The preceding `e477c26` BIN was
`4f30569f0e439137e18899dbec268eacfee0af54f7e55334fb82b86e6842f394`,
with contract `0d0a7a722563f5c15ea927d24241d61bca3652e647fa5918d9987b5001f1b8c2`.
See the [consumer correction record](../docs/firmware-assurance.md#consumer-transition-recovery-correction-2026-10-18).
The old `6a29277` profile had BIN hash
`94ba2f708b69a754f26698cad700d0969601322c6bb692a38c8533cb60e3d2e4`
and ELF contract
`85e38624332094e6e06d9b9c9e0d150a0dc8729b9c0fce57782a325077aaf5b8`.
The emulator still does not model connected USB.

The driver copies six required CHGame core sources and their headers into
ignored `build/assurance/src`, applies `upstream.patch` with zero fuzz, and builds
only headless runners. The dependency stays unchanged. The patch adds guards
before fast/cached CPU execution, loads/stores, bus accesses, CSR accesses and
trap entry. Unknown CSRs cannot reach `csr_misc`. All exceptions fail, including
ECALL and EBREAK. Invalid FENCE encodings fail. The signed store-immediate decode
uses unsigned bit extraction to remove a C undefined left shift without changing
the RISC-V immediate. No full vendor CPU source is checked in here.

## Obligations and controls

Each case has a 4,800,000-cycle budget, nominally 100 ms at fixed 48 MHz. The host
process also has a 60-second timeout. A final instruction can cross the cycle
boundary. Instruction counts alone never establish progress.
Application cases allow at most 480,000 cycles (10 modeled ms) between completed
foreground loops, and independently between validated SysTick returns.
Both deadlines start at reset and apply through the final boundary.
The gate checks them before each instruction and after each scheduling slice.
Sleep cannot bypass these checks. A partial final loop or interrupt is allowed
only while both deadlines remain satisfied.

1. Cold entry with every RAM byte seeded nonzero (`1 + (offset * 73) % 255`).
2. Warm entry retaining RAM from the successful cold normal run.
3. Warm entry with the valid two-word retained rescue request.
4. Warm entry with only the first request word valid.

At `SystemInit`, the runner checks all 144 data bytes against flash, all 4688 BSS
bytes for zero, the ELF-derived GP/SP, and the unchanged eight `.noinit` bytes.
It observes the `SystemInit` return, startup MRET, main entry, and request
consumption. Normal cases require at least three ordered six-row matrix scans,
keyboard task returns and full main-loop completions. Rescue requires at least
three complete transport loops and no QMK scan. Each case requires at least three
SysTick entries, observed returns with restored integer registers/SP/PC and a
timer increment, then further foreground completion. Stack checks use the ELF's
2048-byte budget. Guest flash stores and flash-controller commands fail before
side effects; only the latency register is supported.

Two regression groups run automatically:

- Model fixtures in `runner.c`: all 193 upstream XW golden encodings execute
  twice, with uncached then cached decode. The upstream `xw_test` also runs.
  A synthetic program executes startup CSR writes, MRET and SysTick/HPE return.
  Table-driven negative controls cover wrong-family and unknown registers,
  widths/directions, unknown CSR, illegal instructions/FENCE, ECALL/EBREAK,
  cached execution/memory bounds and denied flash commands. Synthetic legacy
  PLLRDY-wait and self-loop fixtures must fail the same progress obligation check
  at the budget boundary. These are local fixtures, not archived firmware images.
  USB W1C behavior has a positive fixture.
  Two late-stall controls execute the exact candidate for 50 modeled ms first.
  One stops foreground work while interrupts continue to return. The other stops
  inside SysTick. Each must fail its specific progress deadline with flash unchanged.
- Artifact fixtures in `test_artifact.py`: changed compiler-comment text passes.
  Mismatched BIN/ELF bytes, a changed runtime symbol, segment permissions,
  entry point, or ABI flags fail before guest execution.

## Model sources and limits

Per-register address, width, direction and write masks live in `model.c`.
An unlisted access fails with its PC and address. Registers are not admitted by
whole-peripheral ranges. Supported GPIO, TIM1, PFIC and SysTick semantics use the
pinned CHGame implementation behind these checks. The accepted register subset
is intentionally narrower than hardware.

Primary register references are OpenWCH's
`EVT/EXAM/SRC/Peripheral/inc/ch32x035.h`, `ch32x035_usb.h`, and
`EVT/EXAM/SRC/Core/core_riscv.h`. `Peripheral/src/ch32x035_dbgmcu.c`
identifies `0x035106x1` as C8T. The C8T selection is a synthetic assumption,
not an observed CK5200 chip ID. Synthetic POR is `0x08000000`; warm reset is
`0x10000000`. These replace CHGame's bootloader-specific reset flags.
Both profiles start with `mstatus=0x1800`, zeroed general registers, `mtvec=0`,
no enabled or pending PFIC interrupts, and disabled SysTick. Warm cases retain RAM,
but reset the CPU and peripherals. Startup enables interrupts before setting the
vector base. These initially quiet interrupt conditions are assumptions, not
evidence about the OEM handoff.

The loader places only the exact BIN at `0x2000`, starts there, and supplies the
explicit synthetic serial word `0x12345678` at physical `0x0800F600`. Other flash
starts erased. No bootloader or F700 metadata is installed. HSI is modeled ready
when enabled; X035 CFGR0 has no legacy PLL/SW/SWS fields. Fixed modeled timing
does not prove clock timing on hardware.

USB is disconnected. Documented register storage, idle SIE status and INT_FG
W1C semantics are modeled. DMA addresses are checked but no DMA runs. No host
packets, enumeration, mount, reset event or transfer completion are synthesized.
GPIO has no pressed keys. The attached CHGame LCD/SD/buzzer code is linked only
because its bus module references it; no CK5200 external board devices are
claimed. Unknown models fail rather than returning zero.

This gate proves the stated executed paths under this model. It does not prove
all instructions/states, physical matrix wiring, USB operation, OEM boot,
installation permission, rescue retention through the OEM boot stage, or safe
flashing. The BIN and ELF are rehashed after execution; neither is modified.


## Progress-aware watchdog model

The same strict gate now models software-enabled IWDG using HSI/1024, prescaler
32, and reload 4000. After AAAA, the nominal interval is 131,104,768 cycles
at the modeled 48 MHz. CCCC's first counter value is not specified by the
available manual. The model uses the conservative 12-bit maximum of 4095,
or 134,217,728 cycles at prescaler 32, until the first AAAA. Healthy startup
must reach AAAA before either plausible 4000 or 4095 initial-count deadline.
A stuck status poll must reset by the conservative maximum. Register fixtures check the unlock keys, reserved bits, access widths,
update status, one-way enable, reload, and reset state. Unsupported operations
fail. The accepted 32-bit accesses match the pinned vendor SDK; this subset does
not claim every hardware access mode.

Thirteen exact-BIN cases cover normal and rescue execution beyond a full timeout,
late foreground loss, a non-returning SysTick, initialization stalled after arm,
missing scan or transport marks, missing tick increments, rescue restart into
normal execution, two consecutive initialization resets, pre-SystemInit stalls
in normal and rescue startup, and a stuck post-enable status flag. Fault cases suspend
the ordinary 10 ms progress rejection only while the named injected fault is
active, so the modeled hardware watchdog can expire. Healthy cases retain the
existing progress assertions. Missing-mark and missing-tick inputs are explicit
fault injection, not observed hardware behavior.

Expiry resets the CPU and modeled peripherals while retaining RAM, then executes
the actual image startup and validates data/BSS initialization and retained
request handling again. Flash bytes remain unchanged. A repeated fault produces
a reset loop; this is evidence of bounded resets, not recovery. The watchdog
cases have a separate 360-second host timeout. The existing four startup cases,
strict instruction and memory checks, late-stall controls, and artifact mutation
controls remain required.

Software protection starts at the straight-line reset prefix's enable store.
The OEM boot stage and the short entry/configuration prefix before that store
remain outside protection. All startup polling and data/BSS loops follow enable.
The model uses the software-watchdog reset profile; the actual IWDG_SW option
word is unverified. HSI failure, connected USB, and physical reset behavior
remain outside this model. Timing and installation limitations
are recorded in [the program record](../docs/firmware-assurance.md#watchdog-candidate-and-verification-boundary).
