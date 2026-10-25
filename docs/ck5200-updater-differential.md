# CK-5200 updater differential

Checked: 2026-10-23. Verdict: [NO-GO](ck5200-installation-feasibility-verdict.md).
The [iOS instruction record](ck5200-original-ios-investigation.md) owns iOS
claims. Android behavior is independently read from retained decompilation of
the pinned APK, principally `p002a1/s.java`, `T0/o.java` and `L0/a.java`.
V122 addresses refer to the pinned stock executable.

Our implementation is `ios/ClicksInspector/Sources/CustomKeyboardProtocol.swift`,
`CustomFirmwareController.swift` and `StockFirmwareTransaction.swift`.
The published configuration name `ExtensionInstaller` does not identify a
separate updater class. Its flash flow is `CustomFirmwareController.flash`.

## Five-way comparison

| Stage | Original iOS | Original Android | Clicks Inspector | Preserved physical evidence | Consequence |
| --- | --- | --- | --- | --- | --- |
| Identification | EA accessory model/firmware, ordinary EASession | Android accessory service and selected firmware | Exact CK-5200, hardware `1.2.0`, V122 and fresh version before extension staging | Trial recorded expected stock target/mode | Our stricter gate did not block this transfer. No demonstrated causal mismatch. |
| Selection | Manifest exact model and first newer item. Manual CK-prefix EAF route also exists | Manifest exact model, newer versions, highest version (`p002a1.b.m`). Asset sideload route also exists | Only pinned bundled extension in ExtensionInstaller | Exact raw and padded hashes match | First versus highest can change selected release when manifest order differs. Not triggered by this pinned transfer. |
| Validation | File SHA-256 matches descriptor. No separate host vector/length/hardware check on traced path | Download hash checked in `p002a1.o.m`. Asset route `p002a1.q.m` bypasses that hash check | Hash pin, structural reset/vector checks, target checks and bounds | Accepted exact extension | Our extra checks cannot establish copier acceptance. |
| Preparation | Raw downloaded Data, unchanged | Raw supplied byte array | 128-byte staging alignment with trailing `FF` | 27,058 + 78 = 27,136 | Material difference. Padding cause remains unresolved, not proven. |
| Length | Source Data count for A1 and A3 | Byte-array length | Padded count | `0x6A00` in A1/A3 | V122 A1 bound accepts it. OEM copier bound is unknown. |
| A1 encoder | `06 A1`, left-pad big-endian integer to four bytes | Same, `T0.o.b` | Same, `beginUpdateRequest` | `06a100006a00` | Exact bytes match. |
| A1 response | Count >2, type `02`, opcode `A1`; ignores status/capacity | Same type/opcode-only check | Full framing/status and advertised maximum | Accepted max `0x6A00` | Stricter client behavior is irrelevant to an accepted normal reply. |
| A2 encoder | <=32 source bytes. Four offset bytes copied from prior reply slice | <=32 source bytes. Prior reply decoded then encoded as four-byte offset | <=32 bytes, exact cumulative offset | All 848 frames and payload hashes match | Under the captured advancing replies, byte-for-byte parity is established. |
| A2 advancement | Source index advances even if returned offset is partial/backward | Source chunk advancement with returned offset | Rejects zero, partial, backward or skipped progress | All 848 exact advances | Vendor weakness is not triggered by these recorded replies. |
| A2 short write | One stream write, returned count ignored | Accessory write/read service differs from NSStream | Tracks remaining write bytes and exchange timeout | No physical short-write count was captured | Plausibly consequential under faults, but no observed client-byte loss. Cannot prove device flash contents. |
| Receive framing | Each ordinary NSStream read publishes one Data response, no general length-based accumulation | Android service read buffering, not an iOS lifecycle substitute | Length-based fragment assembly, notification filtering, strict reply match | Client accepted all normal ACKs | Difference matters for fragmented/lost/coalesced replies. It does not explain normal recorded frames by itself. |
| Timeout/retry | Writer waits three seconds. No worker packet retry | Worker `p002a1.s.j` aborts failed A1/A2/A3, no second install phase | No unsafe A3 retry. Session-generation and durable uncertainty guards | A1/A2/A3/A0 accepted | No evidence of a missing vendor retry required for installation. |
| A3 encoder | `06 A3` + same source length | Same, `T0.o.a` | Same with padded length | `06a300006a00`, payload `00006a00` | Exact bytes match. |
| A3 response | Type/opcode-only check, no committed-size equality | Same type/opcode-only check | Full status and exact committed size | Accepted declared size | Our stronger check did not alter the successful exchange. |
| A0 encoder | `02 A0` | `02 A0` | `02 A0` | `02a0`, accepted empty payload after framing | Exact bytes match. |
| A0 result | Helper returns Boolean, worker ignores it | Requires transport write success, but reads no A0 reply (`p002a1.s.j`) | Validates stock ACK, records request and unverified state | Request and ACK recorded | OEM success semantics are weaker, not a missing installation command. |
| Session lifecycle | EA notifications, session open/close, delegates and run loops | Android accessory lifecycle | Explicit ownership/cancellation and audit | No retained post-A0 physical detach/reattach proof | Client closure and hardware reset remain distinct. |
| Reconnect | Ordinary rediscovery. No demonstrated saved-update resume or worker success verification | Normal service reconnect | Never auto-applies. Pending journal blocks a new install | Journal remains `sentRebootPending` | Do not clear uncertainty from unchanged stock identity. |
| Installation verification | Worker success after A0, no destination readback | Same transfer-worker limitation | Separate capability/acceptance evidence required | Both `0x94` replies are `080294ff00000000` | Custom execution was not physically demonstrated. |
| Recovery | No independent CK-5200 boot/recovery route identified | No matching independent recovery established | M2 application-level rescue only, R3 unmet | Keyboard types, extension unproven | No implementation supplies independently reliable recovery from dead application. |

V122 is the device-side comparator, not a fourth host encoder. Its dispatcher
decodes big-endian size/offset values for A1/A2/A3 and accepts A0 as a reset
request. Its flash helpers do not provide staging hash/readback verification.
See the [OEM contract](ck5200-executable-and-oem-evidence.md#oem-staging-and-reset-contract).

## Causal classification

| Difference | Classification | Can it cause `0x94 -> FF`? |
| --- | --- | --- |
| Padded length versus vendor raw length, and use of maximum `0x6A00` | **Unresolved** compatibility difference | [INFERENCE] Yes, if the unseen copier rejects that length or copies an incompatible extent. No such rejection rule was recovered. |
| Pinned custom bytes instead of manifest stock image | **Unresolved** compatibility difference | [INFERENCE] Yes, if copier validation rejects the extension or startup/hook bytes differ in physical flash. The client hash does not answer that. |
| iOS first newer entry versus Android highest newer version | **Demonstrably irrelevant to this pinned transaction** | Both can choose different releases from an unsorted list. This transaction used neither automatic manifest selector. |
| Manual CK-prefix list and Android asset sideload | **Plausibly consequential for image choice**, not a recovery proof | Neither establishes copier acceptance of an arbitrary selected image. No such vendor route was used in this transaction. |
| Stricter identity/hash/vector/status/capacity/offset/commit checks | **Demonstrably irrelevant to accepted normal replies** | These gates passed for the retained transaction. Their existence does not redirect `0x94` to OEM code. |
| Vendor A0 result ignored versus our unverified state | **Demonstrably irrelevant to transmitted bytes**, consequential to user claims | Changes the success label, not the A0 bytes or device copier. |
| Short-write and framing policies | **Plausibly consequential under faults** | [INFERENCE] Lost bytes can prevent staging. No short write is proven in the captured client transaction. |
| Different reconnect/ownership/journal policies | **Unresolved physical lifecycle observation**, safer client accounting | No retained physical reset/copy observation exists. Our policy does not provide device readback. |
| BLE routes in vendor app | **Demonstrably irrelevant to inspected CK route** | CK-prefix model selection goes to EAF, not PKService. No CK recovery path was found there. |

**Proven causal defects explaining this physical failure: none.**
The vendor's weak success/response handling is demonstrable client behavior,
but it is not a defect in our code and is not evidence that its installed image
differs. No speculative padding, checksum or retry correction was made.

## Exact transfer assertion

`ios_updater_forensics.py` compares reconstructed A1/A2/A3/A0 requests against
the preserved `dispatch` records. It requires 1/848/1/1 matching requests and
transfer SHA-256
`438e7597b069e19ef8307e51e34ba9ab4cd07c0a7256c84713cc321f05a78aa2`.
The saved update ACK audit retains parsed payloads, not a new independent
device-side readback. Raw extension capability replies remain exact fixtures.

Android `T0/o.java` independently uses left-padded size arrays, chunk count+6,
`A2`, four-byte offsets and original source chunks. V122 dispatch and our Swift
protocol match that normal-frame contract. These comparisons do not establish
equivalence of error handling, session timing, installation or recovery.
