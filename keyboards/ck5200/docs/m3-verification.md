# M3 software verification, 2026-10-16

The three software deliverables are implemented and independently reviewed.
Firmware and iOS CI passed at the revisions below. Nothing was installed on a
physical keyboard or phone. M1 and M2 remain accepted. R3 remains UNMET and
physical flashing remains NO-GO. This is software evidence, not M3 hardware acceptance.

## Reviewed changes and passing runs

| Deliverable | Tested PR head | Evidence |
| --- | --- | --- |
| [PR 1: firmware](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/pull/1) | `4bfe36779836be26633c337a73371c0e1dfd8fcb` | [Firmware build, reproducibility and full suite](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/38835306696); [existing iOS diagnostics and simulator suite](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/38835306687) |
| [PR 2: iPhone profiles](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/pull/2) | `9917774e2217a9476b12becd12952ac7e1de6295` | [Swift/C integration, Debug/Release/QMKKeymap builds and simulator workflows](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/38835307176) |
| [PR 3: spare procedure](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/pull/3) | `9baad57d70c03e9fdbee768a431796f8e41458ae` | [Combined stack iOS verification](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/38835306126); independent procedure review; local readiness and link checks |

PR runs test GitHub's merge snapshot. For PR 2, `toolchain.log` records
`ca1cdc0ef0b8c8b4051cb5155a63209583067c4f`, Xcode 16.4, Swift 6.1.2 and XcodeGen 2.46.0.
The artifact is `ios-verification-ca1cdc0ef0b8c8b4051cb5155a63209583067c4f`.
It contains host logs, both simulator result bundles, build logs and exported screenshots.
The final evidence-only documentation commit follows these tested revisions.

## Frozen firmware artifacts

The `ck5200-firmware` artifact in run `38835306696` contains BIN, ELF, HEX, map,
normalized map, disassembly and `SHA256SUMS`. Downloaded copies are under
`keyboards/ck5200/build/m3-ci-38835306696/ck5200-firmware/`.

- BIN size: **26,496 bytes**; application limit 27,136; **640 bytes free**.
- Linked RAM reservation including stack: **6,832 / 20,480 bytes**.
- BIN SHA-256: `94ba2f708b69a754f26698cad700d0969601322c6bb692a38c8533cb60e3d2e4`.
- CI ELF SHA-256: `ef8df7d3bbf94e5518c7c64e18fd5ce7bf66600c23011e8a28eda84138d4d7db`.
- CI raw map SHA-256: `c66bee91ae9db9adb7b2b9d329427b6c1bd99916ad5c5eafe6078db1516d116f`.
- CI normalized map SHA-256: `e494a02a2506b2f34302832cca8ee5881b7e49b116001e6fcd56b5825c6d4d59`.

Two clean builds matched BIN, ELF and normalized map on each build host. The CI
BIN also matches the local macOS BIN. ELF and maps are host-specific; their
local hashes are recorded in [first-boot evidence](m3-first-boot.md). Raw maps
contain random LTO temporary names. Do not claim cross-host ELF/map identity.
No new image is authorized for installation or added to an iOS installer allowlist.

## iPhone workflow evidence

The actual Swift controller tests use the production C keymap handler, pinned QMK,
and transient EEPROM driver. Passed cases include all 144 factory values,
four-layer edits, atomic profile save, relaunch/reconnect, manual reapplication,
per-write readback, lost replies, partial application, timeout, wrong identity,
rescue-mode rejection, stale callbacks and the A0-A3 transport boundary.
The self-test retains confirmed progress if restoration loses its reply.

The QMK simulator test passed editing, saving, app termination/relaunch, reconnect
comparison, explicit reapplication and factory restoration. The stock simulator
test passed recording, export, session replacement and deletion. Existing M1
catalog definitions remain frozen, independent of the corrected QMK layout.

Screenshots were downloaded from the passing PR 2 run and inspected. Convenient
local copies are in `ios/ClicksInspector/build/m3/screenshots/`:

| Screenshot | Shows | SHA-256 |
| --- | --- | --- |
| `qmk-editing.png` | Editing layer 0, row 0, column 0 from `0014` to `0005` | `c0436e4c5f87517277acc2b0224e7cbcdb7b35bfa611b1fd2913934fcf082c2c` |
| `qmk-reapplied.png` | Saved four-layer profile matches RAM after one verified write | `05e4c4002add8c9048a7f1c96f3304e2d4523a4290438c7b0d21be8c0cfb2583` |
| `qmk-restored.png` | All 144 factory values verified; saved phone profile remains | `c9598e9097ef42ec9c848a9439afbe81b04f0b8e4d9374cdea0dbd8dbe2453f2` |

iOS CI retains artifacts for 30 days. The ignored local copies preserve the downloaded
files without committing simulator runtime state or result bundles.

## Review corrections and limits

Independent firmware review found truncated vendor transfers, malformed headers
for certificates larger than one packet, and missing A0 ACK/reset ordering.
Production fixes at `d0f824d` passed red/green regressions and independent re-review.
Independent iOS spec review found no additional blocker. Standards review found
a self-test progress-count defect; its fix and disconnect regression passed review.
Procedure review removed an ambiguous instruction to demonstrate a hardware
partial failure: that case is now offline only, and actual failure stops all writes.

Initial simulator runs exposed test scrolling defects. A full-frame 100-point
margin rejected a usable restore button; removing all bounds allowed a tap near
the home indicator. Final tests check the target center inside the content area
before hittability, stop after failure, and accept nil as the empty value only
after locating an existing text view. Export and restore assertions remain intact.
These test-only fixes were independently reviewed and passed the linked CI runs.

Local Xcode tests were skipped because `xcode-offload`, `external-ssd-root`, and
the required wrappers were absent. Hosted macOS CI supplied the Xcode evidence;
no repository workaround or local wrapper bypass was used.
Physical boot, matrix labels, USB/MFi timing, charging, restoration and recovery
remain untested. The [spare procedure](m3-spare-bring-up.md) is ready for a separate
decision, but a matching spare and a demonstrated restoration route are still absent.
No merge, purchase, physical test or next milestone was performed.
