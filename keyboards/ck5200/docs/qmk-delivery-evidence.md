# CK-5200 QMK software delivery

Host verification date: 2026-10-17. Target: CK-5200 for iPhone 15 Pro Max,
CH32X035. This is a compiled, reproducible application candidate.
Hardware operation and installation remain unverified. No OEM installer ran.

## Source and imported fixes

The build used the uncommitted firmware changes on
`feat/software-only-qmk-delivery`, based on
`497b8e1592344567f1f5452443050b2fa692c814`.
Main comparison: `d981763e6e96b143afd74ded9d11c72603e47039`.

The reviewed source branches were:

- `feat/m3-first-usable-qmk`: `4bfe36779836be26633c337a73371c0e1dfd8fcb`.
- `feat/m3-iphone-keymap-profiles`: `9917774e2217a9476b12becd12952ac7e1de6295`.

Those branches have identical firmware, build scripts, and firmware tests.
The relevant fixes originated in `e172edd9da269393cbdc2240326e51abc4d7c539`
and `d0f824d00b467ac5af13f18c0f4ff4abc5561064`.
Their firmware and regression changes were applied with patches after comparison
against the current M2 code. No branch checkout or cherry-pick ran.

| Imported change | Defect corrected |
| --- | --- |
| USB/HID descriptors | Removed the extra configuration byte. Corrected key and consumer fields, LED output, and the consumer usage range. |
| Keymap | Restored Shift at `(2,3)`, eight shifted SYM outputs, and consumer usage `0x00CF`. |
| HID queue | Preserved press/release edges within one scan. Retained failed transfers for retry and the final release during queue overload. |
| Identity | Read the device serial word at `0x0800F600`. Corrected Identify lengths and the token length. |
| iAP2 | Retained certificate chunks during USB backpressure. Supported certificates larger than one packet and 384-byte vendor transfers. |
| Reset reply | Returned `04 02 A0 00` before reset. Failed, short, expired, or disconnected completions cannot authorize rescue reset. |
| Image checks | Rejected unexpected allocated sections, executable RAM, installer overlap, overflow, and unexpected flash-controller references. |

Existing regressions from those commits were retained. No separate test suite
or new firmware feature was added. The reproducibility script additionally
compares HEX output and retains prior artifacts and build logs.
Comments now refer to this evidence file.

The current application-level rescue guards remain intact: exact stock length
and stored-flash digest, authenticated session, elapsed-time expiry, pending-marker
entry guard, cancellation on disconnect, and bounded staging writes.
The linker, family gate, startup, rescue entry, and RAM-only settings backend
required no changes.

iOS profiles, older documentation, and CI changes were omitted from this firmware
delivery. They do not change the compiled candidate and have separate ownership.

## Dependencies and build environment

All five existing `external/` repositories were clean before and after the build.
The source dependencies used these pins from `scripts/pins.env`:

| Dependency | Commit |
| --- | --- |
| QMK | `3d4da6de29c8635c9cd232ce456d8fec8d31921b` |
| TinyUSB | `efa3e132235aefa4ef49d31c746c0c5f0c0e0bc9` |
| OpenWCH CH32X035 | `c6253942b29ff2a3f5e2829182a874afffa45881` |
| SHA-256 | `565f65009bdd98267361b17d50cddd7c9beb3e6c` |

The clean CH32V20x cache at `baef0054588f4826548429ff4e7a9257d752ef2f`
was not linked. No dependency checkout or download was needed.

- Compiler: xPack GNU RISC-V Embedded GCC arm64 `15.2.0`, package `15.2.0-1`.
- Compiler executable SHA-256: `d21f58713e7f2f9a9ef9de8762b2c2d1995cd4cdeda724d5fe4834f3af4737b2`.
- CMake `4.4.4`, Ninja `1.13.2`, Python `3.14.8`, pytest `9.1.1`.
- Host C compiler: Apple clang `21.0.0 (clang-2100.3.34.2)`, arm64.
- Build: `MinSizeRel`, `rv32imac_zicsr_zifencei`, `ilp32`, `-Oz`, LTO,
  `nano.specs`, `nosys.specs`, and the project linker script.

Compiler warnings remain: upstream QMK declarations and signedness, the existing
TinyUSB MCU macro redefinition and deprecated `tud_init`, and newlib syscall stubs.
Host warnings concern unused functions and QMK integer-to-pointer casts.
The build and tests completed with exit status zero.

## Exact verification commands

Commands ran from `keyboards/ck5200`. Existing `.toolchains/`, `.venv/`,
`external/`, and `.stock/` files supplied all inputs.

```sh
export TMPDIR=/private/var/folders/t6/jmkhfw452wx9x27cvtj03qmh0000gq/T/opencode
bash -n scripts/check-build.sh scripts/test.sh
bash scripts/check-build.sh
set -o pipefail
UBSAN_OPTIONS=halt_on_error=1 bash scripts/test.sh 2>&1 | tee build/software-only-evidence/tests.log
.venv/bin/python tools/validate_image.py build/ck5200_qmk.bin --elf build/ck5200_qmk.elf
.venv/bin/python tools/stock_audit/extract_ident.py .stock/iKeyboard_CK-5200_V122_120.bin build/software-only-evidence/iap2_ident_data.h
cmp firmware/platform/ch32x035/iap2_ident_data.h build/software-only-evidence/iap2_ident_data.h
git diff --check
```

`TMPDIR` above is this machine's temporary directory, not a firmware requirement.
Use an existing writable temporary directory on another machine.
The comparison script runs `cmake --build build --target clean` before each build.
It then runs `scripts/build.sh`, which checks dependency commits and validates
the generated BIN against its ELF.

Both clean builds produced identical BIN, HEX, ELF, and normalized map files.
This result proves repeatability with this toolchain and checkout path.
Cross-machine and different-toolchain repeatability were not tested.
Only host paths and random GCC LTO names are removed from the normalized map.
The raw map retains those names.

The full existing host suite passed: 20 Python cases, USB descriptors,
update protocol, backlight, consumer keys, keymap, constrained staging,
15 iAP2 cases, four auth-driver cases, real QMK core, and production scanner/transport.
QMK, transport, and rescue harnesses used AddressSanitizer and UndefinedBehaviorSanitizer.
All seven rescue modes passed: `normal`, `rescue`, `early-failure`,
`closed-session`, `watchdog-jump`, `watchdog-packet`, and `watchdog-ep3`.
The wrong-family compilation failed as expected.

Staging tests covered retained flash across power cycles, partial programming,
invalid metadata, exact stock/custom/stock byte transfers, and bounds rejection.
Rescue tests rejected corrupt stock, incomplete transfers, stale sessions,
and unauthorized resets. Sentinel bytes outside staging remained unchanged.
These are host models, not tests of the OEM installer's writes.
The image validator's flash-access check is compiler-specific, not a general
proof about arbitrary memory writes or hardware protection state.

## Artifacts and memory

Artifacts remain in ignored `keyboards/ck5200/build/`:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `ck5200_qmk.bin` | 26496 | `94ba2f708b69a754f26698cad700d0969601322c6bb692a38c8533cb60e3d2e4` |
| `ck5200_qmk.hex` | 74567 | `f0a9f16cdc93eb668b4225b9816454659f42847185254af6dbba71353fbe2c46` |
| `ck5200_qmk.elf` | 45140 | `0ad0afccae603914cdf90e9298ef3da2f3e0a62c6ea7e877a730ba0d7dd1d298` |
| `ck5200_qmk.map` | 164225 | `13da930fc76a0f7fcc345be49f9e3475c649aea3dc5db71513ede4c03ed2e9f6` |
| `ck5200_qmk.map.normalized` | 114899 | `fededfa0420433a3f40b4f728288c531494c6364e9a18c2a44ecc5be23336f19` |

`build/software-only-evidence/repro.SdghBS/` retains both build logs, the first
build, the hash manifest, and prior artifacts copied before cleanup.
`build/software-only-evidence/` also contains validation and test logs,
ELF layout, symbols, disassembly, dependency provenance, and `source-sha256.txt`.
The source manifest SHA-256 is
`731893cd7fca75714ff59b9b3509b954b65f9c7c051edcf72a51eaf98a0a847f`.
It covers firmware, CMake, scripts, tests, and the two changed tools.
It excludes this evidence document and parent-owned work.

All ranges below exclude the upper endpoint. Flash addresses use the linked
alias unless the `0x08000000` physical base is explicit.

| Region | Verified layout |
| --- | --- |
| Installer | `[0x0000,0x2000)`, outside candidate load segments |
| Application allocation | `[0x2000,0x8A00)`, 27136 bytes |
| Candidate image | `[0x2000,0x8780)`, 26496 bytes, 640 bytes spare |
| Entry/reset | Entry `0x2000`, JAL `0x6F50506F` to `0x7EF4` |
| Code and constants | 26352 bytes including reset/vector sections |
| Initialized data | 144 bytes, flash source `[0x86F0,0x8780)` |
| RAM | `[0x20000000,0x20005000)`, 20480 bytes |
| Static RAM | 144 data + 4632 BSS + 8 retained rescue bytes = 4784 bytes |
| Stack budget | 2048 bytes, startup stack top `0x20005000` |
| Linker RAM usage | 6832 bytes including stack provision, 13648 bytes spare |
| Staging writes | `[0x08008A00,0x0800F500)`, aligned 256-byte pages |
| Device/reserved pages | `0x0800F500` gap, `0x0800F600` serial, `0x0800F700` config, reserved from `0x0800F800` |

The linked ELF passed startup, section, vector, load-segment, and RAM checks.
Runtime stack high-water usage was not measured.

## Stock reference and limits

The local stock file `.stock/iKeyboard_CK-5200_V122_120.bin` is exactly 18648
bytes. Its SHA-256 is
`8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
The file ends at linked address `0x68D8`.
The unknown tail `[0x68D8,0x6900)` is not present in that file.
The candidate spans that tail. The linker does not preserve it.
Neither application bounds nor staging bounds establish installation permission.
Any overwrite of that unknown tail remains outside the authorized mutation range.

The stock decode places Shift at `(2,3)` in `0x2AE6`, and adds fixed SYM Shift
bits in `0x2A86..0x2AB0`. The host scanner tests cover all 36 crossings,
eight shifted symbols, seven cursor crossings, modifier chords, and release edges.
Physical keycap positions and iPhone behavior remain unverified.
Configuration-dependent Z/J behavior is not reproduced.
Navigation uses the existing hold layer instead of stock's runtime mode.
Keymap and brightness settings remain RAM-only and disappear on reset.
Global debounce can delay keys during chatter, as the existing QMK test records.

Safe mode is application-level rescue, not independent recovery.
It needs working startup, USB, iAP2, and the application image.
Retention of its RAM request through the proprietary boot stage remains unverified.
Power loss during the stock-style marker page write can leave an unsafe marker.
Host tests record that residual risk rather than proving recovery from every failure.

No device access, USB inspection, flashing, ISP command, physical reset,
protection change, purchase, or vendor contact occurred.
Hardware protection state, physical typing, and OEM installation checks were skipped
because this delivery is software-only.
