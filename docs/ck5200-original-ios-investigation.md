# Original Clicks iOS investigation

Checked: 2026-10-23. Primary executable SHA-256:
`d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b`.
All addresses below are linked virtual addresses in this executable.
This is static instruction analysis and a behavioral reconstruction, **not
execution of the iOS application**. Android does not fill any iOS edge.

## Inventory and method

The decrypted 3.0.2 IPA has 213 non-directory members. Each member's path,
length and SHA-256 is reproduced by `ios_updater_forensics.py`. The complete
inventory stays in ignored output to avoid publishing vendor resources.

| Relevant component | Evidence and confidence |
| --- | --- |
| `Payload/Clicks.app/Clicks` | 9,771,792-byte ARM64 Mach-O, `cryptid 0`, pinned hash above. HIGH for byte identity, provenance/authenticity remains UNVERIFIED. |
| Main `Info.plist` | Bundle `com.clickscompanion.app`. EA protocols `com.clickscompanion.protocol` and exact typo `com.clicks.protocl`. HIGH. |
| `PlugIns/ClicksActivityExtension.appex` | WidgetKit extension, bundle `com.clickscompanion.app.ClicksActivity`. Not an EA updater extension. HIGH for declared role. |
| `Frameworks/{FirebaseAnalytics,GoogleAppMeasurement,GoogleAppMeasurementIdentitySupport}.framework` | Three embedded measurement frameworks. Their executable hashes are in the complete inventory. No model-matched loader resource identified. |
| Resource bundles | Alamofire, CocoaLumberjack, AppAuth, GoogleSignIn, GoogleUtilities, Firebase, nanopb, leveldb, Promises, SnapKit and ProgressHUD, among others. Bundle presence alone does not prove updater reachability. |
| Firmware resources | No member name or leading gzip/ZIP/XZ/LZ4 signature identifies a firmware/loader package. The main executable contains distribution URLs. |
| Downloaded image | Documents directory, firmware URL `lastPathComponent`, selected at `0x10017B7B8`. This is a path derivation, not a recovered phone cache. |
| Entitlements | Inspected executable signature: app/team identifier, APNs production, Apple sign-in, empty associated-domains and app-groups arrays. No demonstrated independent keyboard recovery entitlement. |
| Alternate device service | Swift `PKService`, Bluetooth device information and firmware command metadata. Reachability is evaluated below, not inferred from names. |

Methods: LLVM ARM64 disassembly, function starts, Mach-O load commands, static
Swift array objects, Swift field records, Objective-C method/selector stubs,
direct branch cross-references, ADRP/ADD literal references, and closure/async
continuation targets. Function-window hashes are retained in the evidence ledger.
Import stubs identify Foundation, CryptoKit, Combine and Dispatch operations.

The local annotated windows are under
`ios/ClicksInspector/build/installation-feasibility-20261023/`.
They remain private. `inspect.py` in that directory supports `window`,
`function`, `xref`, `literal` and `strings` queries against retained disassembly.
The committed verifier independently checks byte identity, constants and key BL
edges without requiring that scratch helper.

## Instruction-supported workflow

| Stage | Executable edge or operation | Result |
| --- | --- | --- |
| Discover | `0x1001A43A0`, `0x1001A44D4`, `0x1001A44F8`, `0x1001A4504` | EA local notifications, connect/disconnect observers, enumerate connected accessories. |
| Protocol match | `0x1001A45AC–0x1001A4600` | Search accessory `protocolStrings` for configured protocol. Default literal `com.clickscompanion.protocol` at `0x1006021A0`. The second plist protocol is not proof of a second updater. |
| Connect | ObjC `accessoryDidConnectWithNotification:` `0x1001A4888` → `0x1001A46A0` → `0x1001A4928` | Bind matching accessory. The function logs EA identity fields, including model/firmware/hardware. |
| Bind session | `0x1001A50DC–0x1001A511C` → `0x1001CB2F8` → `0x1001CBD64` | Create SessionHandler and EASession. |
| Open | `0x1001CBDB4–0x1001CBF48` | `initWithAccessory:forProtocol:`, input/output delegates, input on main run loop, output on current run loop, `open`. An initial input read is also made. |
| Identify | `0x10017B550` / `0x10017B5B0`; EA model/firmware selectors | Selection uses EA `modelNumber` and `firmwareRevision`, not an A1 boot-mode identity. Hardware is logged but no separate hardware selector exists in the four-field descriptor. |
| Manifest URL | `0x100180FA4` → `0x1000E6E48` | Input 0 selects `https://xinyi1.clicks.tech/firmwares.json`. Input 1 selects `https://xinyi1.clicks.tech/firmwares-p.json`. Input 2 reads the published setting, then uses the same select. |
| Fetch/parse | `0x1001810D8`, JSONDecoder `0x10018118C–0x1001811A4`, response dispatch `0x100181254` | Indirect Alamofire request/response calls. Callback `0x1001813AC` → `0x10017C464` passes decoded array or error to its closure. |
| Select | Closure `0x100181468` → `0x10017B218` | Iterate in supplied order. Require exact model equality and a newer version. Return the **first** qualifying item, not a separately calculated maximum. |
| Version order | `0x100110DD4–0x1001114B8`, call `0x10017B630` | Split decimal components on `.`, compare numerically, missing/unparsable components use zero in inspected conversion paths. `-1` means current version is older. |
| Manual image list | `0x1000D7D5C`, `0x1000D7E28–0x1000D7EF8` | Uppercase model prefix `CK` gives FirmwareSource tag 1 (`eafRemote`). Prefix `CP` gives tag 2 (`bleRemote`). Other remote models get tag 3. |
| Transport dispatch | `0x1000D81C8`, tag-1 branch `0x1000D82F0–0x1000D8380` | EAF download entry `0x1001775F0`. Tag 0 enters local BLE. Tag 2 enters remote BLE. CK-5200 does not use PKService through this route. |
| Download | `0x1001778B4` → `0x10017B93C` | Async frame passes selected descriptor and continuation. Destination closure `0x100180B50` → `0x10017B7B8` uses Documents plus URL basename. |
| Download result | `0x100180CE0` → `0x10017BC20` | Response/error handling resumes continuation. Successful download leads to image validation at `0x10017A740`. |
| Load/hash | `0x10017A808`, `0x10017A8F0` → `0x10011158C` | Read file as Data. The hash helper independently reads the file, uses CryptoKit SHA256 (`0x1001115B4`, `0x1001117B8`), formats digest bytes as `%02x` and joins them. |
| Validate | `0x10017A914–0x10017A960` | Compare computed string with expected `sha256sum`. Failure bypasses transfer. No host length/CRC/signature/vector/hardware validation was found in this path. |
| Schedule worker | `0x10017AC9C`, `0x1001812B4` → `0x10017AD48`; closure `0x100181340` → `0x10017B05C` | Original loaded Data is captured on a global Dispatch queue. No padding/header/footer transformation occurs along this Data path. |
| Transfer | `0x10017B098/0CC/104`, `0x10017B0DC`, A3 representation branches, `0x10017B184` | A1 → all A2 source chunks → A3 → A0. Multiple representation branches are not retries. |
| Finish | `0x10017B188–0x10017B1F4` | Select success immediately after A0, without testing its Boolean result. Resume Swift continuation. A1/A2/A3 failures use tags 2/3/4. |
| UI return | Async resume `0x1001778CC` and result publication `0x100177AD0–0x100177B6C` | Return worker result to UI. No consequential second install command or new-identity requirement on this completion path. |
| Disconnect | ObjC `0x1001A48DC` → `0x1001B44B4` → `0x1001A665C` → `0x1001CBF80` | Clear accessory, close streams, remove run-loop scheduling, clear delegates and session. Clear related service state. |
| Reconnect | Normal discovery/connect route above | Recreate ordinary application session. The inspected worker does not resume a saved transfer or require a changed version before reporting success. |

## Actual encoder

Static Swift array element bytes begin at object address +32:

| Object | Bytes |
| --- | --- |
| `0x10084DFD0` | `06 A1` |
| `0x10084DFA0` | `A2` |
| `0x10084DF70` | `00 00 00 00` |
| `0x10084DF40` | `06 A3` |
| `0x10084DF10` | `02 A0` |

The integer helper `0x100111A80` converts to uppercase radix-16 text, prefixes
an odd number of digits with zero, and parses hex pairs. The resulting byte
array is minimal-width, big-endian. A1/A3 prepend zeros until four bytes exist.
The append helper `0x10004EC78` allocates/copies as required, copies source
elements with `memcpy` at `0x10004ECE8`, then increases array count. It performs
no checksum, encryption or reordering.

```text
size_bytes(n) = minimal_big_endian(n), left-padded to at least 4 bytes
A1(n) = 06 A1 || size_bytes(n)
A3(n) = 06 A3 || size_bytes(n)
A0    = 02 A0

source_chunks = split raw Data into slices of at most 32 bytes
offset_bytes = 00 00 00 00
for source_chunk in source_chunks:
    frame = byte(source_chunk.count + 6) || A2 || offset_bytes || source_chunk
    reply = synchronous_stream_exchange(frame)
    require reply.count > 2 and reply[1] == 02 and reply[2] == A2
    offset_bytes = reply[4..<8]
    advance source-chunk index independently of the returned offset
```

Anchors: A1 `0x100179BC4`; A2 `0x100179E54`; split helper `0x10011201C`
with cap `0x20` at `0x100112184`; A2 reply slice `0x10017A0E4–0x10017A0F4`;
A3 `0x10017A2B4`; A0 `0x10017A544`.
No status-byte or exact cumulative-offset check exists in these wrappers.
A short A2 reply can pass the three-byte check and then violate the Data slice
precondition. No resynchronization or packet retry loop was identified here.

Deterministic, nonproprietary examples:

```text
A1 V122 size 18648:         06a1000048d8
A1 raw extension 27058:     06a1000069b2
A1 padded transfer 27136:   06a100006a00
A2 source 00 01 02, off 0:  09a200000000000102
A3 padded transfer 27136:   06a300006a00
A0:                        02a0
```

The last raw-extension A2 header is `18a2000069a0` plus 18 source bytes.
The last padded-transfer A2 header is `26a2000069e0` plus 32 padding `FF` bytes.
All 851 preserved update requests match reconstructed vendor frames exactly
when supplied the same padded bytes and advancing replies.

## Stream and async semantics

Wrappers enter synchronous queue closure `0x100181488` → `0x10017ECCC` →
`0x1001CB86C`. The writer checks `hasSpaceAvailable`, makes one
`write:maxLength:` call at `0x1001CBA2C`, ignores its return value, and waits on
a semaphore with a three-second deadline (`0x1001CBA4C–0x1001CBA7C`).
No-space/missing-stream and timeout paths return no response. They do not
drain/retry the request.

The input delegate handles event value 2 and an open input stream. It reads at
most 1,024 bytes at `0x1001CC6AC` and constructs Data from that read. It does not
assemble ordinary replies by their declared length across multiple reads.
The first-byte `0B` path handles keyboard events. `0x1001CD438` filters exact
four-byte `04 04 01 xx` cursor and `04 04 02 xx` USB-mode notifications.
Other read blocks publish as the response through barrier closure
`0x1001CD7D0` → `0x1001CCE68`, replace stored response, then signal the semaphore
at `0x1001CCEC4`. A special 15-byte keyboard-event suffix path exists at
`0x1001CCB70–0x1001CCD8C`. It is not a general stream-frame accumulator.

The worker's A1/A3 calls depend on Foundation Data representation. They do not
form repeated attempts. A1/A2/A3 failures stop the worker. A0 failure does not.
An unchanged V122 identity on reconnect does not retrospectively verify the
vendor success state. No equivalent of our durable pending journal was found
on the inspected EAF route.

## Alternate routes and search limits

The app exposes a firmware list and BLE local/remote source cases. Metadata
alone originally left PKService ambiguous. The executable model-prefix and
enum dispatch above now separates CK/EAF from CP/BLE. No CK-5200 recovery
transport or loader resource was identified in the examined routes.
Ordinary application commands, BLE firmware commands, analytics, notifications
and WidgetKit do not establish independent rescue from a dead application.

Search scope: complete decrypted IPA inventory, main executable relevant
strings, direct calls to each A helper, EAF worker closures/continuations,
manifest fetch call sites (`0x1000ACD64`, `0x1000D70C0`, `0x100183324`),
manual firmware-source dispatch, session create/teardown, disconnect/reconnect,
and retained Android resources/manifests. No rollback, second commit/reset,
saved staging resumption or boot-stage download was found on these paths.
This is not proof that no unreachable or runtime-generated route could exist.

## Exact remaining analysis limits

| Site/edge | Methods attempted and limit | Impact |
| --- | --- | --- |
| `blr x25` at `0x1001810D8` / `0x100181254`, download `blr x24` at `0x10017BB10` / `blr x28` at `0x10017BBA8` | Traced argument construction, class metadata access, JSONDecoder setup, captured closures and callback bodies. Full statically linked Alamofire dynamic dispatch and OS URLSession scheduling were not executed. | HTTP framework behavior is modeled as file/error delivery. It does not replace the directly traced raw Data-to-worker path. |
| Published setting access `0x1000E6EB0` | Traced both return URL constants and indirect Combine getter. Indirect Swift symbolic type/name resolution was attempted. One parser variant timed out and was discarded. Exact setting UI/storage semantics remain unresolved. | Both manifest choices converge on the same EAF encoder. No recovery semantics are inferred from the setting. |
| Full Swift/ObjC async runtime and stream races | Function-starts, selectors, closure captures and continuation targets were inspected. The device-target iOS Mach-O cannot execute as a macOS process or as a Simulator-target binary. Executing its OS frameworks would require a compatible iOS runtime/device environment not available within these constraints. | Behavioral tests are reconstruction, not original-app execution. Race timing, cancellation during framework download and every OS error ordering remain UNVERIFIED. |
| `Assets.car` and arbitrary encoded blobs | All archive members were hashed and classified. No filename, recognized compressed member or traced updater reference identifies a loader. Asset-container internals and arbitrary encrypted/encoded payloads were not exhaustively decoded. | Scoped negative finding only. No matching OEM boot-stage bytes were recovered. |
| High-level UI completion outside inspected EAF callbacks | Worker, result return, discovery, manual source routing and teardown were traced. Not every SwiftUI rendering specialization or analytics callback was decompiled. | No claim that every app function is reconstructed. No unexamined UI rendering is treated as evidence for installation or recovery. |

These limits prevent a claim of complete original-app execution. They do not
justify replacing iOS evidence with Android behavior or skipping the encoder.

## Reproduction and behavioral cases

```sh
python3 ios/ClicksInspector/Tests/verify_official_app.py com.clickscompanion.app_3.0.2_und3fined.ipa.ipa
python3 keyboards/ck5200/tools/ios_updater_forensics.py \
  --ipa com.clickscompanion.app_3.0.2_und3fined.ipa.ipa \
  --apk keyboards/ck5200/.stock/clicks-app-prod-release.apk \
  --evidence ios/ClicksInspector/build/post-install-capability-evidence-01 \
  --output ios/ClicksInspector/build/installation-feasibility-20261023/ios-model
```

The tool checks the executable hash, array constants, call instructions, 851
captured frames, 18 synthetic worker cases and seven selection/validation
assertions. Each worker case records outbound bytes, supplied reply, write
return, modeled wait and final state. Cases cover A1/A2/A3/A0 loss, status
rejection, wrong opcode, short write with reply, partial progress, fragmented
reply, disconnect during staging and disconnect after commit.

Wrong model and unchanged version select no newer image. The traced selector
does not check hardware revision separately. Invalid hash stops before transfer.
No host maximum-length check rejects `0x6A01` before A1. Device acceptance must
not be inferred from that absence. A2 partial progress advances the source
index anyway. A0 loss/rejection still yields reported success.
The version fixture covers ordinary nonnegative dotted decimal release strings.
Malformed Unicode, signed overflow and every Swift String representation are
outside this behavioral model, not silently replaced by Python parsing claims.

[CORRECTION] Earlier unresolved-encoder wording in the memory/OEM-transition
note understated existing direct iOS evidence. The older stock-manager record
already contained correct wrapper/constants evidence. This investigation adds
the helper data flow, response framing, lifecycle and complete four-command
reconstruction. It does not convert vendor success into installation proof.
