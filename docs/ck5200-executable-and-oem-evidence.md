# Exact executable, OEM installation and recovery evidence

Checked: 2026-10-23. [Decision: NO-GO](ck5200-installation-feasibility-verdict.md).

## Genuine startup boundary

The existing CHGame-based runner now has a `startup-probe` mode. It executes
from application entry `0x2000`, with OEM CRT and the exact image loaded at its
linked address. It makes no synthetic USB, authentication, settings, stack or
callback handoff before stopping. It does not substitute a new SystemInit.

| Image | SHA-256 | Executed instructions before boundary |
| --- | --- | ---: |
| Stock V122, 18,648 bytes | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` | 1,358 |
| Exact extension, 27,058 bytes | `2f5400a30abf4dc6a2c7fa0652d666a421c9255e8eb3750b7c02dfde84e74132` | 14,600 |
| Exact transfer, 27,136 bytes | `438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2` | 14,600 |

All three stop on **`lw a4,0(a5)` at `0x3BBE`, reading RCC CTLR at
`0x40021000`**, immediately after SystemInit entry `0x3BBA`.
The runner reports:

```text
V122 profile: unsupported memory access at pc=0x3bbe value=0x40021000
```

The exact raw and padded images produce identical startup trace hashes:
`72d072a1bf7fe9c20e39d246ffc677fe5d028183a37daeed84ed2c5f5eb356c6`.
Both execute startup hook `0x48CE` → `0x70B2` (`ck_extension_init`).
Original `.data` source `0x6880`, OEM BSS and extension BSS checks pass in the
separate bounded runner path. Entry, GP/SP, CRT and extension initialization
are executable evidence. RCC readiness, subsequent peripherals, USB setup,
EP0 enumeration and iAP authentication are not closed by this test.

The profile has no validated RCC clock/reset model. Returning a ready bit or
skipping SystemInit would substitute a synthetic condition. That was not done.
Stock hitting the same boundary prevents mislabeling this as a custom defect.

## Original receive path, separately bounded

`receive-probe` uses the existing explicit synthetic USB/authenticated fixture
after CRT. It executes original registration helper `0x4776`, checks that
`0x20000524` holds `0x2208`, then feeds each captured request through original
adapter `0x629E`. The call at `0x62BC` reaches OEM command entry `0x2208`, hook
`0x2218`, exact target `0x715C`, then response submission `0x62CC`.

The original adapter requires authenticated state 3 at context+2. The fixture
supplies that state. It is not a measured physical authentication exchange.
Registration executes as a bounded call, not as a claim that full startup
reached registration past the RCC boundary.

Captured request fixtures:

```text
0c940100547becbf00000000
0c940100a60a007000000000
```

| Control | Observed bounded result for both requests | Meaning |
| --- | --- | --- |
| Unmodified V122 | `080294ff00000000` | OEM path rejects extension command. |
| Exact 27,058-byte image | 27-byte `CKX1` response with each exact request nonce | Intended command entry executes. |
| Exact 27,136-byte image | Same capability result | Trailing padding does not change these modeled execution paths. |
| Command hook disabled at `0x2218` | `080294ff00000000` | Deterministic missing-hook reproduction. |
| Startup initializer skipped at `0x48CE` | Host guard: extension BSS not cleared | No guest reply is claimed. |
| Extension region erased from `0x6900` | Unexpected guest trap | No guest reply is claimed. |
| Corrupted startup SP instruction at `0x4864` | Stack enters reserved region, host guard stops | Chosen corruption is detected. Not exhaustive startup-fault coverage. |
| Registered command callback cleared in fixture RAM | No reply, extension entry not reached | A null callback does not reproduce the observed `FF` response. |
| Intentional `0x715C` redirect to OEM `0x3216` | `080294ff00000000` | Reproduces rejection after reaching the patched entry address. |

These controls establish possibilities, not the physical flash contents or
physical root cause. None is an installable artifact. Full startup remains
blocked. No substituted USB condition closes that gate.

Reproduction:

```sh
python3 keyboards/ck5200/stock_extension/assurance/run.py
python3 keyboards/ck5200/tools/executable_installation_probes.py
python3 keyboards/ck5200/tools/post_transfer_forensics.py \
  --evidence ios/ClicksInspector/build/post-install-capability-evidence-01 \
  --output ios/ClicksInspector/build/installation-feasibility-20261023/forensics \
  --ios-executable tmp/ck5200-tail-evidence/und/Payload/Clicks.app/Clicks
```

Derived mutants/traces stay under build directories. The new probe script
requires its output under `keyboards/ck5200/build`. It publishes response,
address, boundary and hash summaries, not proprietary image bytes.

## OEM staging and reset contract

The V122 executable, not a replacement loader, supports these facts:

| Operation | Known behavior | Evidence boundary |
| --- | --- | --- |
| A1 | Accept size up to `0x6A00`, reset RAM state/cursor, initialize invalid marker in RAM | This does not immediately write an invalid marker to flash and does not prove boot-stage size acceptance. |
| A2 | Decode big-endian offset, stage source data, advance RAM cursor and report progress | ACK progress is not flash readback or a staging hash. |
| A3 | Accept cursor equality **OR** declared-length equality, commit using stored A1 length, reply using stored length | Do not replace this with an AND requirement. ACK does not independently validate marker/payload flash. |
| A0 | Request reset and acknowledge in the stock application | The stock application does not copy staging into the application region. No captured hardware-reset completion follows from the ACK. |
| Application destination | Images link at `0x2000` | Actual copier's validation and copy extent remain unknown. |
| Staging layout | Marker `0x8A00`, payload `0x8A04` | Device contents were not read back. |

Rechecked helper chain: `0x3174` → `0x31B2`, flash unlock `0x4956`, erase
`0x49D4`, buffer reset `0x4982`, buffer load `0x49AA`, page program `0x49FE`,
lock `0x4974`. Polls at `0x4998`, `0x49C2`, `0x49EC` and `0x4A14` read
STATR `0x4002200C` and mask only BUSY `0x01`.

The inspected chain does not test write-protection error `0x10` or EOP `0x20`,
compare programmed bytes, retry failed pages, time out BUSY, or propagate an
independent programming-success result into A2/A3. A stuck BUSY can hang rather
than acknowledge. A programming error with BUSY clear can proceed to ACK without
the missing integrity checks. This is a possible failure, not an observed one.

Maximum A1 length, final-page flush, marker handling and 128-byte staging
alignment are application protocol facts. They do not establish copier rules
for signatures, checksums, image length, final page, destination bounds,
partial-copy rollback, marker invalidation or boot retries.

## Preserved physical chronology

All times are UTC, 2026-10-23, converted from retained record timestamps.

| Event | Time |
| --- | --- |
| Last A2 ACK, cumulative `00006a00` | `06:56:11.213262` |
| A3 ACK, committed-size payload `00006a00` | `06:56:11.225189` |
| A0 dispatch | `06:56:11.227195` |
| A0 validated ACK | `06:56:11.231726` |
| First `0x94` rejection | `07:26:52.827859` |
| Second `0x94` rejection | `07:27:33.243560` |

The saved update audit does not record post-A0 physical detach/reattach. Earlier
review of the 11 retained stock/checkpoint bundles did not establish that event
either. A session close, an accessory notification, USB reset, MCU reset,
boot-stage execution and application restart are different observations.
Absence from these records proves none of them occurred or did not occur.
The explicit user observation is that normal typing still works.

## Distribution and boot-stage discovery

The six retained model-matched releases are raw application images with vector
word `0x2000`, not recovered complete device flash. Their sizes are not multiples
of 128:

| Version | Bytes | Remainder modulo 128 | End when loaded at `0x2000` |
| --- | ---: | ---: | --- |
| V114 | 16,828 | 60 | `0x61BC` |
| V115 | 17,452 | 44 | `0x642C` |
| V116 | 17,460 | 52 | `0x6434` |
| V117 | 18,296 | 120 | `0x6778` |
| V121 | 18,572 | 12 | `0x688C` |
| V122 | 18,648 | 88 | `0x68D8` |

`matching-release-manifest.json` records model, revision, full firmware URL,
advertised hash and matching downloaded hash. The app descriptor fields are
`model`, `firmwareRevision`, `file`, `sha256sum`. No separate hardware-revision,
signature or boot-stage URL field exists in this descriptor. URL basename
`_120` is not independently decoded as a copier rule.

All 213 IPA members and 1,183 APK members were inspected by path, hash and
leading executable/compression signature. The IPA contains five Mach-O files
and no recognized compressed member or named firmware/loader candidate. APK
candidate `DebugProbesKt.bin` begins `CAFEBABE` and is a JVM class, not an MCU
image. Its only leading gzip candidate,
`okhttp3/internal/publicsuffix/publicsuffixes.gz`, expands to 115,042 bytes of
public-suffix data, not model-matched loader code. No traced EAF path unpacks an
auxiliary loader. This is scoped evidence, not proof against arbitrary hidden
encoded data in every resource.

Public search was bounded to these three queries on 2026-10-23:

- `"CK-5200" "bootloader"`
- `"CK-5200" "flash dump" recovery firmware`
- `"Clicks" "CH32X035" firmware recovery`

Results were retail listings, unrelated keyboard/device recovery instructions,
generic WCH documentation and unrelated CH32 projects. No credible complete
CK-5200 dump or matching copier was found. The unrelated
[CH32 USB power meter](https://github.com/krzysztofgawrys/ch32x035-usb-power-meter)
and [generic CH32X035 product page](https://www.wch-ic.com/products/CH32X035.html)
are not CK-5200 recovery evidence. Existing retained archive/public research
remains valid within its recorded bounds. The search stopped without acquiring
another loader or contacting support.

The actual OEM `[0x0000,0x2000)` bytes remain unavailable. App resources,
application-only firmware releases and generic MCU documentation cannot
establish the absent copier's decisions or prove independent recovery.

## Recovery

R3 remains **UNMET**. No independently reliable iPhone-only route from an
unbootable application is demonstrated. M2 is application-level rescue, not
independent recovery. A working stock keyboard today does not establish rescue
after an incompatible image replaces it. No new physical test is justified.
