# Software-only recovery: target-loader evidence beyond PR6

Checked 2026-10-17 at `497b8e1592344567f1f5452443050b2fa692c814`, branch `feat/software-only-qmk-delivery`.

**No factory-loader restore path meets the current constraints. This is a bounded evidence result, not proof of universal impossibility.**

New findings improve the decision:

- A public source claims CH32X03x factory entry when application flash is blank. Its exact test and loader revision remain unknown.
- Public CH32X035 replacement loaders implement standard DFU, nonzero writes, or RAM execution. Their target code is available. None establishes these features in the factory loader.
- Static inspection covered seven host binaries and their PE resources. No matching X035 factory image was identified.
- Whole-page erase cannot preserve arbitrary unknown bytes in the last page throughout an update. PR6's counterfactual partial erase is not that hardware operation.

## Scope and evidence levels

Only public-source retrieval, static file analysis, and offline arithmetic occurred. No downloaded target code or WCH program/library ran.
No device operation, USB command, reset, flash operation, protection change, publication, or contact occurred.
All added evidence and scripts remain in `tmp/software-only-loader/`. This note is the only durable deliverable.
The earlier four notes were read before the search. Their exhausted download routes were not repeated.

Permanent exclusions apply to all conclusions and next actions. Replacement-loader installation is not a recommendation or an approved next phase.
The parent investigation owns iOS APIs and stock startup. This note supplies target-side evidence for that work.

| Layer | Established here | Still unknown |
|---|---|---|
| MCU hardware | Existing WCH evidence specifies 256-byte erase pages and separate, replaceable System FLASH. Read protection constrains writes from System FLASH and RAM. | Actual CK5200 options, initial configuration, and System FLASH contents. |
| Factory loader | Public clients encode A4 count and A5 address. Independent public documentation adds a blank-flash entry claim. | Actual X035 A4/A5/A6 implementation, reset dispatch, descriptors, implicit erase, and interruption effects. |
| OEM firmware | Supplied V122 occupies `[0x2000,0x68d8)`. Earlier static work establishes staging behavior. | Installer bytes below `0x2000`, destination-tail policy, and independent recovery interface. |
| Host tool | Two additional maintained implementations expose conventional ISP behavior. Host binaries contain no identified matching BOOT artifact. | Host support does not settle an unimplemented target command or hidden target behavior. |

The hardware statements above use the pinned primary evidence in [boot-entry.md](boot-entry.md#cited-findings-1) and [isp-preservation.md](isp-preservation.md).
They are not new measurements of the keyboard.

## 1. Hypothesis: blank or invalid code selects factory recovery

### New positive lead

`basilhussain/wch-web-isp` explicitly states that CH32X03x ignores control pins and enters its bootloader when application flash is blank.
The same paragraph says the loader, rather than hardware, reads the control pins after power-on.

- Current inspected source: [index.html:191–194][W-entry], commit `65b094522de1ac215dbf95710d3a6737ceff3420`.
- Introduction: [commit `e3893c1201e79f967b33094d04a224f20e678482`][W-entry-change], 2025-04-02.
- The change replaces a tentative power-on statement with the stronger entry claims. It adds no dump, disassembly, test trace, or loader version.
- The repository tree contains a JavaScript host implementation, not factory target code.

**[UNVERIFIED]** This is evidence of a public claim, not verification that CK5200 has that behavior.
It prevents treating PC17-high selection as the only conceivable entry condition.
It does not establish a hidden immutable ROM fallback. The known BOOT region is System FLASH.

### Why this does not yet solve damaged-app recovery

"Blank" is not equivalent to a bad image, invalid instruction, corrupt vector, failed checksum, or nonresponsive application.
The source does not define its blank test, the inspected address range, or whether zero words also count as blank.

**[INFERENCE]** A check at main offset zero could see the intact OEM installer while the application at `0x2000` is damaged.
Such a check would not provide the required damaged-application recovery. A whole-flash blank test would have the same limitation.
Creating blank flash would also conflict with the preservation constraints and is excluded.

**Falsifier / next proof:** A public matching factory image could show the reset path reading the app boundary or checking image validity.
The needed trace runs from reset entry to the condition, then to USB initialization or the application jump.
Record the exact addresses read, comparison values, reset-source checks, and selected transport.

**Software-only availability:** The public claim and its history are available now. A matching target image was not located in this search.
No device experiment is needed to test the claim once a legitimate public image exists.
Until then, use separate model branches for "blank prefix," "blank entire flash," and "invalid app." None is observed CK5200 behavior.

## 2. Hypothesis: another implementation reveals selective factory writes

### Maintained clients do not supply the missing target trace

| Public implementation | Exact read | Result |
|---|---|---|
| `wagiminator/MCU-Flash-Tools`, `f72f516c37895aad7adcd208f30416ef45e7a214`, latest commit 2026-07-15 | [chprog.py:167–232,378–379][C] | Detection can issue A8 when X035 RDPR is `0xff`. Flash sends count-only A4, then A5 from zero. It accepts reply byte `0xfe` or `0xf5` without raising its write/verify error. |
| `basilhussain/wch-web-isp`, `65b094522de1ac215dbf95710d3a6737ceff3420`, latest commit 2026-05-20 | [command.js:150–187][W-command], [session.js:189–283][W-session] | A4 has one count. A5/A6 serializers have an address. Write/verify loops start at zero, with an empty final A5. This is a serial host, not a new target protocol. |
| `frank-zago/isp55e0` public support report | [PR #17][I-report], head `c2927053d8a8ede542f901abeb447fd6effb8e04` | F7P6 `57 23` and F8U6 `5e 23` report loader `00 02 06 00`, RDPR `a5`, and successful 63,488-byte image comparison. It has no nonzero-start or preserved-neighbor trace. |

The `chprog` behavior adds a concrete reason that a success report from another client is insufficient evidence.
These source reads reject those clients as preservation solutions. They do not disprove a target extension.

### A useful cross-family counterexample

The [CH32V003 missing manual:352–451][V-manual] states that its A4 ignores the count and erases all application flash.
It also describes A5 page buffering, a zero-length flush, and unconditional success responses.
At [lines 453–513][V-manual], it describes a latched A6 failure that subsequent comparisons do not clear.

This is another chip, with 64-byte pages. It is not the X035 implementation.
It demonstrates why identical host packets and nominal ACKs cannot establish target erase extent or durable completion.
The repository contains documentation only. It supplies no X035 dump.

### Actual X035 target counterexamples, correctly classified

**DFU replacement:** [`krzysztofgawrys/ch32x035-dfu-boot`][D], commit `c0858c6f49f532ae23624ad4b3d44e9917b94e53`.

- [config.h:16–30][D-config] places this loader at `[0,0x2000)`, with the application at `0x2000`.
- [boot/main.c:40–63][D-main] treats the first application word as invalid when it is `0xffffffff` or zero. It then enters DFU.
- [boot/dfu.c:91–100][D-dfu] declares interface class `0xfe`, subclass `1`, protocol `2`.
- [boot/dfu.c:121–150,249–263,401–405][D-dfu] starts at the app base, buffers data, erases 256-byte pages, and writes them.
- The final flush fills unused bytes with `0xff`. Its upper bound permits the full app region through `0xf800`.

This is source-level proof that another X035 loader can implement nonzero programming with implicit erase and standard DFU.
It is not factory A5. It would replace the unknown OEM installer and would not preserve an arbitrary last-page tail.
It therefore fails this task's constraints as a deployment path.

**System-FLASH replacement:** [`cnlohr/ch32fun`][F], commit `a800cbd1f1ca5e250c73da83c669f0965d2052e5`, latest commit 2026-10-07.

- [examples_usb/bootloader/README.md:3–18,32–36][F-readme] expressly calls it an alternative to factory ISP, tested on X035 and CH570.
- [bootloader.c:160–170,245–280][F-target] accepts HID data into a RAM buffer and calls code from that buffer.
- [minichlink/pgm-b003fun.c:1173–1239][F-write] reads partial-page neighbors, constructs full pages, erases their span, then writes them.
- This explains the source's "non-destructive" wording: it restores neighbors after erase. It does not guarantee that those bytes never change.

The [2026-09-07 public report][J] describes this replacement loader writing at a nonzero X035 offset with 256-byte alignment.
Its installation prerequisite changes the loader. Its suggestion that "unbrick" restores factory BOOT is explicitly untested.
The inspected [DefaultUnbrick:3211–3306][F-unbrick] changes options and requests erase. It does not contain a factory-image restore in that function.
No part of that procedure is an authorized next step here.

**Rejected conclusion:** "X035 cannot support selective writes, RAM execution, or DFU" is too broad.
**Supported conclusion:** These capabilities exist in public replacement code. They remain unproved for the installed factory/OEM loaders.

**Falsifier:** A matching installed-loader artifact with equivalent handlers, or a public version-matched trace proving their effects, changes this conclusion.
**Software-only availability:** The replacement source is available and was read. Its presence in CK5200 is not established.
Static comparisons against a future legitimate factory/OEM artifact are permitted. Installing either replacement is excluded.

## 3. Hypothesis: a public binary contains the actual factory loader

### Artifact acquisition and resource inspection

The previous search listed archives and host strings. This pass additionally parsed PE resource trees and scanned whole binary files.
It used existing, provenance-recorded downloads from [isp-loader-followup.md](isp-loader-followup.md#source-register-and-reproduction-anchors).
The failed current WCH-LinkUtility endpoint was not retried.

| Container or member | Size | SHA-256 |
|---|---:|---|
| Mirrored `WCH-LinkUtility/WCH-LinkUtility.exe` | 2,637,784 | `117f035adc8a6cb4bf3ebfb92d3d2e1e1e31d82825df79cf457b2e5ebf0331fe` |
| Mirrored `WCH-LinkUtility/McuCompilerDll.dll` | 1,667,032 | `df84d5d70f074fcd62cb1938a67f56a67ca3c02d7368a7a57102beefd688b327` |
| Official setup `WCH55xISPDLL.dll` | 139,280 | `52354dfba178b2be30f72850d13ec2ee5ea12b71151dc5c4ffcd5cea8af09761` |
| Official setup `WchIspStudio.exe` | 4,665,872 | `e01bd0830b60a79cbad3e27c596dc09eb1b65bc9a44ee85189717e82a17f054d` |
| Official setup `WCHISPTool_CH32Vxxx/WCHISPTool_CH32Vxxx.exe` | 3,923,984 | `dd10d318ddc4546e1d3baad8ce092365bb5149a925fd4b1c676a2ff9c66e2ebf` |
| Official setup `Update/IspUpdateTool.exe` | 1,788,432 | `4d92d63e87c3f3d102898d923074cb7ce292c5d80c7a6a70af1741ea7044be7a` |
| Official setup `Update/IspAutoInstallToolA.exe` | 1,777,168 | `c2347ed4dea0c2dff99b200a25443d2b678baa7b7d0723bc14f9de428aea99d7` |

Official setup URL: <https://www.wch-ic.com/download/file?id=196>.
Archive SHA-256: `8d2004251dcb59d43c63a132f341a76dca99b863fc03c1556987b679e8afac95`.
Mirror provenance: [WeAct fixed commit][L-mirror], archive SHA-256 `e0c09421a7f38df5015ca52c2e94a10a9c50b5bf4eca58c052c7c9b1583850ac`.

The offline scanner records resource paths, file offsets, lengths, and hashes in `resource-scan.json`.
It does not import, load, or execute any inspected binary.
LLVM `llvm-readobj --sections --coff-resources` independently listed 84 resources in the mirrored Link EXE.

Findings:

1. Link EXE/DLL resources are UI/version/manifest groups. Link resource `[241,135,2052]` is only 38 bytes at file offset `0x25cb24`.
2. The CH32V host's `CDB/195/2052` resource is 10,376 bytes at `0x2568c8`. Its header identifies a Keil uVision configurator database.
   Its readable records name CH55x devices and header files. This is not an identified X035 BOOT image.
3. `IspAutoInstallToolA.exe` has `SHE/130/2052`, 49,705 bytes at `0x16d788`, SHA-256 `7902fe354d01d10d0bddd5a4dc64822aa90fd383c2c28ce12eaa4ec20089015d`.
   It starts with `.SHE` and LZMA properties at offset 10. An offline raw-LZMA decode produced 648,629 bytes without an end marker.
   The apparent stored length is 648,628. This decode is incomplete as a format interpretation, and is not authenticated as target code.
   **[INFERENCE]** Repeated color-like byte patterns suggest UI image data. That is a lead, not a completed exclusion.
4. No whole-file candidate matched the scanner's WCH USB-device descriptor filter in the seven binaries.
   The filter requires length/type `12 01`, a standard USB version, legal EP0 size, and VID `0x4348` or `0x1a86`.
   It cannot detect compressed descriptors, constructed descriptors, a different VID, or UART-only code.

**Result:** No legitimate matching factory image was identified, so no X035 factory disassembly is claimed.
Resources hidden in ordinary data arrays or other compression remain outside the proof.
These exact limits are stronger than either "no `.bin` file exists" or "the factory payload cannot be obtained."

### Public dump and embedded-blob false positives

- `ch32fun` contains [`misc/factory_bootloader.bin`][F-bin], but its tree length is **1,920 bytes**.
  [minichlink/chips.c:1–15,123–134][F-chips] associates 1,920-byte BOOT with V003, and 3,328-byte BOOT with X035.
  The filename alone does not make that file an X035 image. No matching X035 provenance was found.
- [`pgm-wch-linke.c:998–1015,1046–1088`][F-flashblob] selects and sends blobs through a debug programmer protocol.
  Those `BootloaderBlob` objects are flash-writing helpers, not evidence of factory USB A4/A5 handlers.
- The reviewed [EEVblog thread][E] supplies neither an X035 factory attachment nor target disassembly.
  Its linked later multidrop binary is a custom V003 loader.
- GitHub issue search for `CH32X035 "bootloader.bin"` returned an [rv003usb report][V-report].
  Its log identifies V003. X035 appears in a linked failure discussion, not in a matching BOOT artifact.

**Falsifier:** A public artifact with X035 provenance, matching memory layout, and traceable reset/USB/flash handlers would reopen this route.
**Software-only availability:** The inspected containers are available. The matching factory payload is not established within them.
The concrete remaining artifact action is static classification of the `SHE` container, then host resource-load references if it contains executable data.
That action can run offline. It requires no contact or device operation.

## 4. DFU, MFi, and independent iPhone access

Factory ISP's known A1–A8 bulk protocol is not USB DFU merely because it updates firmware.
The public DFU replacement demonstrates the distinction with actual interface descriptors and class-request handlers [D-dfu].
The ch32fun replacement instead supplies HID handlers and RAM execution [F-target].

**[UNVERIFIED]** No factory X035 descriptor set, DFU handler, MFi protocol implementation, or CK5200 loader accessory declaration was recovered here.
The OEM application's supported accessory protocol does not prove that an independent loader exposes that same protocol.
Conversely, a bulk-ISP client does not prove the device has no additional interface.

The falsifier is an installed-loader descriptor/configuration table plus the handler that serves the relevant interface.
An independent iPhone app also needs an applicable public API. That separate requirement belongs to the parent's API investigation.
The source-only result here is "no identified compatible loader interface," not "an iPhone can never communicate with this MCU."

## 5. PR6: valid model, unsupported physical promise

PR6 correctly separates the following:

- A packet encoder from proof of a target operation.
- Factory System FLASH from the OEM installer in main flash.
- Unknown data from assumed `0xff` padding.
- A hypothetical in-memory result from real restoration.

At this repository pin, [lab.py:119–127,215–224](../../keyboards/ck5200/recovery_lab/lab.py) explicitly labels its contract counterfactual.
Its last `erase_page()` changes only `min(256, END-address)` bytes. At `0x6800`, that is 216 bytes.
The remaining 40 bytes never change in the model. This is not a documented CH32X035 erase primitive.

**[CORRECTION]** A 256-byte physical erase plus tail backup/rewrite cannot provide this model's strict continuous-preservation guarantee for arbitrary unknown tail values.
An erase maps every byte in `[0x6800,0x6900)` to `0xff`. Any tail byte initially unequal to `0xff` changes before rewrite.
An interruption after erase exposes the loss. A later successful compare cannot undo that history.

This is a conditional physical contradiction, not a universal recovery impossibility result:

- It applies when the last page must be erased and arbitrary tail bytes must remain unchanged throughout.
- Avoiding erase can only cover compatible bit transitions. It does not prove restoration from arbitrary damaged contents.
- A proven narrower primitive would change the premise. No such factory operation was found.
- Knowing a tail is disposable would change the preservation policy. Its absence from V122 does not supply that knowledge.

The hypothetical capability remains useful for checking model gates and interruption logic.
It must not be presented as an unimplemented host option or evidence that a physical whole-page algorithm can satisfy the same promise.

## 6. Target-trace coverage and stopping point

| Required target trace | Coverage after this pass |
|---|---|
| Factory reset to blank/invalid test | Public claim with a pinned introduction. No matching factory instructions. |
| Factory A4 dispatch to actual erase loop | No recovered target trace. Host count encodings are covered. |
| Factory A5 initial nonzero address, page crossing, final flush | No recovered target trace. Replacement DFU source supplies a counterexample for a different loader. |
| Factory A6, A2, timeout, and disconnect side effects | No matching trace. V003 documentation warns against assuming generic semantics. |
| Factory descriptors and DFU/MFi handler | No matching artifact. Replacement DFU and HID source are covered. |
| Strict final-page preservation | Whole-page implementation conflicts with the stated guarantee for arbitrary unknown bytes. |
| OEM installer route after damaged application | Remains with the parent investigation. No factory result establishes it. |

The bounded public search used these routes:

- Web queries: `CH32X035 bootloader dump`, `CH32X035 bootloader disassembly`, `CH32X035 bootloader.bin`, and blank/implicit-erase variants.
- GitHub repository search: `ch32x035 bootloader`, followed by the matching public DFU source and ch32fun source.
- GitHub issue searches: `CH32X035 "offset" "bootloader"`, `CH32X035 "erase" "A5"`, `CH32X035 "blank" "bootloader"`.
- Source history for the new blank-flash claim and PE resources from the already obtained host distributions.

Search responses and source trees are saved in scratch. Search absence is not an exhaustive proof about all public artifacts.

**Strongest defensible path:** Continue static work on the already available OEM artifacts for an existing independent recovery handler.
In parallel reasoning, retain factory blank-flash entry as an unverified branch, rather than ruling it out by host-tool limits.
A factory route still requires target semantics, preserved unknown bytes, independent entry, and a reachable phone interface together.
None of the new counterexamples satisfies those gates for CK5200.

**Next factory-specific software action:** Complete the `SHE` resource classification and follow any executable payload references statically.
If it is only UI data, stop that artifact route. Do not substitute another host-client implementation for the missing factory target trace.

Checks completed: pinned-source reads, public commit history, resource enumeration, per-member hashes, descriptor-candidate scan, and model arithmetic review.
No repository tests ran because this task changes only a research note. No physical validation or vendor-binary execution occurred.

## Pinned source links

[W-entry]: https://github.com/basilhussain/wch-web-isp/blob/65b094522de1ac215dbf95710d3a6737ceff3420/index.html#L191-L194
[W-entry-change]: https://github.com/basilhussain/wch-web-isp/commit/e3893c1201e79f967b33094d04a224f20e678482
[W-command]: https://github.com/basilhussain/wch-web-isp/blob/65b094522de1ac215dbf95710d3a6737ceff3420/modules/command.js#L150-L187
[W-session]: https://github.com/basilhussain/wch-web-isp/blob/65b094522de1ac215dbf95710d3a6737ceff3420/modules/session.js#L189-L283
[C]: https://github.com/wagiminator/MCU-Flash-Tools/blob/f72f516c37895aad7adcd208f30416ef45e7a214/chprog.py#L167-L232
[I-report]: https://github.com/frank-zago/isp55e0/pull/17
[V-manual]: https://github.com/basilhussain/ch32v003-bootloader-docs/blob/d957856a400d2b27d02d38634b3223d4b721071e/README.md#L352-L513
[D]: https://github.com/krzysztofgawrys/ch32x035-dfu-boot/tree/c0858c6f49f532ae23624ad4b3d44e9917b94e53
[D-config]: https://github.com/krzysztofgawrys/ch32x035-dfu-boot/blob/c0858c6f49f532ae23624ad4b3d44e9917b94e53/config.h#L16-L30
[D-main]: https://github.com/krzysztofgawrys/ch32x035-dfu-boot/blob/c0858c6f49f532ae23624ad4b3d44e9917b94e53/boot/main.c#L40-L63
[D-dfu]: https://github.com/krzysztofgawrys/ch32x035-dfu-boot/blob/c0858c6f49f532ae23624ad4b3d44e9917b94e53/boot/dfu.c
[F]: https://github.com/cnlohr/ch32fun/tree/a800cbd1f1ca5e250c73da83c669f0965d2052e5
[F-readme]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/examples_usb/bootloader/README.md#L3-L36
[F-target]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/examples_usb/bootloader/bootloader.c#L160-L280
[F-write]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/minichlink/pgm-b003fun.c#L1173-L1239
[F-unbrick]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/minichlink/minichlink.c#L3211-L3306
[F-bin]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/misc/factory_bootloader.bin
[F-chips]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/minichlink/chips.c#L1-L134
[F-flashblob]: https://github.com/cnlohr/ch32fun/blob/a800cbd1f1ca5e250c73da83c669f0965d2052e5/minichlink/pgm-wch-linke.c#L998-L1088
[J]: https://www.ipc.shimane-u.ac.jp/weda/SetupDiary/2026/20260907/
[L-mirror]: https://github.com/WeActStudio/WeActStudio.WchLinkW/blob/d02cf4fd01047746e1a2e27f3490d43d4038a2a4/WCH-LinkUtility%20.ZIP
[E]: https://www.eevblog.com/forum/microcontrollers/bootloaders-and-cheap-risc-v-(ch32x035-and-ch32v003)/msg5865765/#msg5865765
[V-report]: https://github.com/cnlohr/rv003usb/issues/76#issuecomment-5557817717
