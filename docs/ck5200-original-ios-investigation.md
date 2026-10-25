# Original Clicks iOS investigation

Checked: 2026-10-25. Primary executable SHA-256:
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
| `Assets.car` and arbitrary encoded blobs | The 2026-10-25 audit below resolves all 511 main-catalog data assets as WebP images. The widget catalog has no data assets. Arbitrary hidden encodings remain outside this check. | No loader data asset was identified. This does not prove that arbitrary encoded payloads cannot exist. |
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

## Software-only follow-up, 2026-10-25

The software investigation remains open. Missing boot-stage bytes do not prove
that hardware access is required. These checks ran without commands to the
keyboard. They establish bounded findings, not universal software exhaustion.

### Public releases and archives

All three public catalogs returned HTTP 200 with JSON request headers.

| Catalog on `https://xinyi1.clicks.tech/` | Bytes | Entries | Exact CK-5200 entries | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `firmwares.json` | 12,006 | 52 | 6 | `e1b84cad26d872c8560a221518c28e3d162d4daa6bf92f801722a4a0fa133633` |
| `firmwares-p.json` | 827 | 4 | 0 | `6f274fbcd315bdadee59fce6f4eae407ac8ef1619429618f031fb19b872a3bd0` |
| `firmwares-a.json` | 881 | 4 | 0 | `a2e36f833da12af5f755e44fe7cf5dff62fe28508aac141d957635e7a2699c82` |

The six CK-5200 versions are V114, V115, V116, V117, V121, and V122.
Their catalog hashes match the retained application images. No catalog entry
identifies a loader, factory image, recovery image, or complete flash dump.
This finding does not exclude unpublished vendor files.

The selector at `0x1000E6E48`, including the published-setting branch at
`0x1000E6EB0`, selects between the two existing URL constants at
`0x1005FF7B0` and `0x1005FF7E0`. The unresolved setting does not supply a
third URL in this function.

The archive search queried all 32 Common Crawl indexes dated 2024 through
October 2026 for `xinyi1.clicks.tech/*`. Every index returned no captures after
retries. The final query for `CC-MAIN-2025-43` used `matchType=domain`.
The all-time Wayback CDX query for this host, filtered to HTTP 200 and collapsed
by URL, returned `[]`. These results cover this host and these query scopes.
They do not establish absence from every archive or alternate host.

### Compiled assets and app shortcuts

The main `Assets.car` is 69,069,192 bytes, SHA-256
`86a3c51c405ba43060fced11d51da5eb1148c0cf7b69fa4b6fdcaec806b7c106`.
`/usr/bin/assetutil --info` reports 511 data assets. All 511 match complete
RIFF WebP payloads by metadata length and rendition hash. The hash covers the
224-byte CoreUI header followed by the complete payload. The metadata field
named `SHA1Digest` contains the 64-digit SHA-256 value for these renditions.
Every parsed RIFF chunk is `ALPH`, `VP8 `, or `VP8X`. No unmatched data asset,
length mismatch, or invalid chunk bound remains in this check.

The widget `Assets.car` is 41,592 bytes, SHA-256
`2f3c20bf08e16583204d46a29922751f946926f02e81a352cc16aec86ba0913b`.
Its ten metadata rows contain no data assets. These checks close the ordinary
embedded data-asset lead. They do not test hidden encodings inside images.

Swift field records identify `NumberRowShortcutSlot` with `slot` and `action`.
The action enum contains `esc`, `volumeUp`, `volumeDown`, `mute`, `playPause`,
`nextTrack`, `previousTrack`, `pageUp`, `pageDown`, and `empty`.
`NumberRowShortcutsView` uses the BLE PowerKeyboard service. No general CK-5200
keymap editor was identified through this view. The earlier CK/EAF source
dispatch remains the relevant update route.

The main app plist does not declare `UIFileSharingEnabled`,
`LSSupportsOpeningDocumentsInPlace`, or `CFBundleDocumentTypes`.
It therefore does not advertise a Files export route for its downloaded image.
The phone's actual app container was not inspected.

### Stock USB and packet-parser paths

The following addresses belong to V122 firmware, not the iOS executable.
The image SHA-256 is
`8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
The retained disassembly is `keyboards/ck5200/.stock/audit/v122.dis`.

| Path | Instruction evidence | Bounded result |
| --- | --- | --- |
| USB vendor request | Callback `0x3CBC` returns immediately. | No vendor readout command in this callback. |
| HID class requests | Class callback `0x3C58` dispatches interface 2. `GET_REPORT` at `0x4300` and `SET_REPORT` at `0x4326` load callbacks initialized to zero at `0x3D46` through `0x3D4A`. | No active report handler was identified. |
| HID descriptor | The 97 bytes at `0x6580` contain six Input items, two LED Output items, no Feature items, and no report IDs. | No advertised Feature-report configuration channel. |
| Descriptor reads | Router `0x4582` selects predefined descriptors. `0x45D2` through `0x4600` clamp reply lengths. String getter `0x3A78` bounds indices to 0 through 5. | No host-selected memory address in these getters. |
| Serial string | `0x2276` through `0x227A` load the fixed word at `0x0800F600`. | Fixed serial data, not an arbitrary read address. |
| Link parser | `0x5BEC` through `0x5F18` trust some declared lengths beyond the actual input. | Unchecked reads exist. No arbitrary write or code execution was demonstrated. |
| Link copy | `0x5E50` through `0x5E88` cap the copy at 32 bytes into an internally initialized buffer. Pointer setup is at `0x6230` through `0x6236`. | This copy does not reach the pending callback at link offset `0x84`. |
| Command `08` | `0x3362` through `0x3368` enforce lengths 5 through 19. Request-dependent stores fit the local frame allocated at `0x3222`. | No saved control data overwrite through this bounded copy. |

The parser audit does not prove that unchecked reads are harmless.
Indirect effects through authentication or protocol state remain unverified.
No malformed packet was sent to the keyboard.

The generic WCH IAP example is a different protocol. Its `iap.h` uses application
base `0x08005000` and commands `80` through `84`. Its verify operation compares
flash with supplied bytes and does not return flash contents. This SDK source
does not establish the protocol of the installed CK-5200 boot stage.

### Remaining software proof

No working stock key-editing or extension-installation route follows from these
new checks. The next runtime evidence is an original stock-app update trace.
The installed boot stage below offset `0x2000` is still unavailable.
Complete startup emulation remains blocked by unmodeled peripherals.
Neither gap authorizes device writes or establishes a hardware-only conclusion.

Private audit outputs for this session are under
`/private/tmp/ck5200-software-routes-20261025/`.
`asset-data-verification.json` records all rendition matches.
`archive-search-summary.json` records the final archive responses.
`release-format-checks.json` records the tested checksum hypotheses.
`software-route-evidence.json` records the route summary and open gaps.
These local outputs contain no firmware payload publication and are not
required to interpret the instruction addresses and scoped results above.

### Stock-app runtime access follow-up

Read-only `devicectl` queries on the paired iPhone identify the installed app as
`com.clickscompanion.app`, version `3.0.2`, build `3`. The app is running.
Its metadata reports `Developer App = false` and `Container Accessible = false`.
Adding `--require-container-access` returns no matching app. This rules out
direct log-file retrieval through the available developer-container service.
Version equality alone does not establish executable byte identity on the phone.

The original executable contains a local log viewer. `AboutView` field metadata
at `0x10072103C` includes `_showLogViewer`. `CKLogViewer` field metadata at
`0x100723604` includes `_logFiles`, `_selectedLogFile`, and `_logContent`.
Function `0x1000FA41C` enumerates registered loggers, selects `DDFileLogger`,
and calls `sortedLogFilePaths`. Function `0x1000FA8DC` reads the selected file
as UTF-8 with Foundation `String(contentsOfFile:encoding:)`.
The opening gesture, release file-logger registration, and EAF packet-byte
coverage remain unverified. A compiled log viewer is not a captured trace.

The local Console automation attempt stopped at Cua Driver's missing Screen
Recording permission. The temporary driver was stopped without changing that
permission. No Console trace, debug attachment, app relaunch, firmware update,
or reset occurred. The installed app's runtime update flow remains unverified.
