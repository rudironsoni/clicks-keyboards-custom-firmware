# CK-5200 installation feasibility, 2026-10-19

## Requested

Test whether the 40 bytes after V122 are a protected region, trace the original
iOS update transition into the stock application, and find a recovery route
that an iPhone can use when the application cannot boot. No device operation
is part of this investigation. QMK v1 remains at
`12cfe69180327ddf2e1ebb82069331f87a1c60a1`.

## Done

### Memory ownership correction

**[CORRECTION] The existing assertion that `[0x68D8,0x6900)` is a known protected
region has no demonstrated OEM ownership basis.** It was the offline laboratory's
conservative rule to preserve every byte outside the supplied V122 file. That
rule is a model precondition, not a recovered hardware partition. This corrects
the factual claim without declaring unread bytes disposable.

The address space distinguishes file offsets, linked addresses, and physical
flash aliases. A linked address `a` corresponds to file offset `a - 0x2000`
inside these application images, and main-flash physical address
`0x08000000 + a`.

| Artifact | Length | Linked extent | SHA-256 |
| --- | --- | --- | --- |
| Official V121 | `0x488C`, 18,572 bytes | `[0x2000,0x688C)` | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |
| Official V122 | `0x48D8`, 18,648 bytes | `[0x2000,0x68D8)` | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |
| Frozen QMK v1 BIN | `0x693C`, 26,940 bytes | `[0x2000,0x893C)` | `969e6d71a177119e19dd0a93fffee4cfc63f2c1b46c01a9ccf01d56ac353694e` |

These stock hashes match the archived OEM release manifest and the previously
archived downloads. A fresh request to `https://xinyi1.clicks.tech/firmwares.json`
returned HTTP 403 in this investigation. No current manifest or new download
is claimed. The local stock bytes were rehashed in this session.

The WCH reference manual V1.9, section 20.1, physical PDF page 238, places
`0x080068D8` in ordinary main flash. It places the manufacturer configuration
at `0x1FFFF700`, user options at `0x1FFFF800`, and the separate launcher at
`0x1FFF0000`. It defines no hardware-reserved partition at V122's end.
This does not exclude OEM data stored in ordinary main flash.

Two executable observations challenge a fixed image-end boundary.

1. V122 contains 76 additional bytes at linked `[0x688C,0x68D8)`, file offsets
   `[0x488C,0x48D8)`. Those bytes lie outside the older V121 file, in the same
   physical page `[0x08006800,0x08006900)`. A rule that permanently reserves
   everything after each release's file end would reject this official growth.
2. V122 instructions at `0x37A8..0x37AE` construct `0x6A00` and branch to the
   accepted A1 path when the requested length is at most that value. The path
   stores the supplied length at context offset 4 and advertises capacity
   `00 00 6A 00`. That capacity corresponds to `[0x2000,0x8A00)`. This proves
   the running application's staging-length check, not the unseen installer's
   copy bounds or image validation.

The startup calculations also explain the file ends. V121 resets to `0x4812`
and copies 88 bytes from `[0x6834,0x688C)` to
`[0x20000000,0x20000058)`. V122 resets to `0x485C` and copies 88 bytes from
`[0x6880,0x68D8)` to the same RAM range. V122 sets `gp = 0x20000840`.
Its AUIPC/ADDI calculations at `0x486C..0x487C` and loop at
`0x4880..0x4890` establish the exact source end. The initialization copy
does not require bytes after the image. The script checks both binaries'
instruction forms and performs these address calculations independently.

The application **does access flash beyond its distributed image**. V122
`0x2276..0x227A` loads the serial source word from `0x0800F600`.
`0x39A0..0x39B4` loads 12 configuration bytes from `0x0800F700`.
The configuration path at `0x39FA..0x3A18` can write that page through the
read-modify-write helper at `0x31B2`. Its byte load at `0x3208` reads unchanged
parts of the selected 256-byte page. The update path writes staging from
`0x08008A00`. Thus, file length is not a complete device-memory ownership map.
These are positive reference findings. They are not an exhaustive proof about
all computed pointers or unread installer code.

**[INFERENCE] The stock application's intended payload window includes the
questioned 40 bytes.** Their actual device contents and any installer-only
use remain **[UNVERIFIED]**. Neither stock file contains those bytes. The
absence of a byte sequence or literal address in an application file cannot
prove that an installer or an indirect data access never uses it.

### Final-page operations and image layout

The WCH manual section 20.2.1 documents 256-byte page programming and erasure.
The pinned SDK at `c6253942b29ff2a3f5e2829182a874afffa45881`,
`EVT/EXAM/SRC/Peripheral/src/ch32x035_flash.c`, provides `FLASH_BufLoad`
at line 664, `FLASH_ErasePage_Fast` at line 685, and
`FLASH_ProgramPage_Fast` at line 729. Four-byte buffer loads are not a
documented independent four-byte flash-program operation.

| Proposed operation | Technical consequence |
| --- | --- |
| Erase the final V122 page, then program a complete replacement page | Ordinary page operation. It changes the whole 256-byte page, including the 40 bytes. Unknown protection can reject it. |
| Read the page, replace its image prefix, erase, and rewrite the saved suffix | Can preserve the suffix after successful completion with a functioning reader and writer. It cannot keep the suffix unchanged during the erase-to-rewrite interruption window. |
| Program without erasing | Not a general replacement method. It cannot supply arbitrary transitions back to erased bits, and no applicable OEM contract establishes safe repeated partial programming. |
| Erase only the 216-byte V122 prefix of the final page | No such sub-page erase appears in the inspected manual or SDK. |
| Use standard sector erase | Section 20.4.3 and SDK `FLASH_ErasePage` specify 1 KiB, so the sector containing the tail is `[0x08006800,0x08006C00)`. This erases a wider range than fast page erase. |
| Keep the entire page untouched | Prevents changing bytes already occupied by both stock and QMK. It requires a different image layout and installer behavior. |

The manual's standard-erase flowchart also says "erase 8 pages at a time",
which conflicts with its 1 KiB sector text and 256-byte page table. This is a
documentation inconsistency. The 256-byte fast operation is consistent with
the pinned SDK and the stock application's staging writer. No exact OEM
installer sector footprint is inferred from the inconsistent diagram.

QMK v1's ELF places executable `vendord_xfer_cb` across the interval. The
instruction at `0x68D6` even straddles its beginning. Deleting or filling the
40 bytes in the existing BIN would corrupt executable code.

A relinked image could move code around a hole, but the evidenced A1/A2/A3
format transfers a contiguous payload. A hole filled with `FF` is still part
of that payload and does not preserve destination contents across page erase.
A sparse HEX file does not add skip semantics to the OEM installer.

The verified image has 196 bytes free below `0x8A00`. A simple additional
256-byte reservation exceeds that budget by 60 bytes before alignment changes.
That arithmetic is not a relink experiment or a proof that optimization is
impossible. A relink would also create a different firmware candidate. No
layout change is justified until it solves an evidenced installer requirement.

### Original iOS updater and stock executable transition

The supplied decrypted Clicks Companion 3.0.2 build 3 executable has SHA-256
`d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b`.
Its Mach-O encryption command has `cryptid = 0`. The existing repository
verifier pins this executable. This establishes the bytes analyzed, not
App Store signature authenticity or provenance of the supplied decrypted IPA.
Those remain **[UNVERIFIED]**. A second local IPA has different executable
bytes and is not used for these instruction addresses.

| Stage | Executable evidence | Established behavior and limit |
| --- | --- | --- |
| Manifest selection | `0x100180FA4` calls `0x1000E6E48`. The latter compares its selector and chooses between URL values at `0x1005FF7B0` and `0x1005FF7E0`. | Manifest choice exists in executable flow. Selected-item routing is traced below. The server's model list and the runtime CK-5200-to-item construction were not executed or fully established by this static pass. |
| Local image loading | `0x10017A808` calls imported Foundation `Data(contentsOf:options:)` at `0x100580258`. The returned data is retained in the async context. | The updater reads the selected local file. This is not an installer or device-memory validator. |
| Image hash check | `0x10017A8F0` calls `0x10011158C`; that helper calls CryptoKit SHA256 metadata at `0x1001115B4` and SHA256Digest metadata at `0x1001115EC`. `0x10017A8F8..0x10017A920` compares the returned string to the expected string, including a call to Swift string comparison at `0x100582BD4`. | Equal hashes reach `0x10017AA34`; missing/mismatched results take the error branch at `0x10017A924`. This checks file identity against the supplied expected hash. It does not validate absent boot code or prove acceptance by the device installer. |
| A1 | Update routine `0x10017B05C` calls wrapper `0x100179BC4` at `0x10017B098`, `0x10017B0CC`, or `0x10017B104`, according to the Data representation. Static prefix `06 A1` is at `0x10084DFD0`. | The wrapper appends four big-endian size bytes and checks response type/opcode. Stock V122 dispatches A1 at `0x3274` to `0x37A8`, accepts length at most `0x6A00`, and stores it. |
| A2 | `0x10017B0DC` calls `0x100179E54`. Its chunk helper at `0x10011201C` caps payload at 32 bytes. The response at `0x10017A0E4..0x10017A0F4` supplies the next four-byte offset. | Stock dispatches A2 to `0x380A`, accumulates 256-byte pages from staging base `0x08008A00` with four bytes reserved for the marker, and calls `0x3174` to erase/program each full page. |
| A3 | Calls at `0x10017B0F4`, `0x10017B17C`, or `0x10017B19C` enter `0x10017A2B4`, using prefix `06 A3` at `0x10084DF40`. | Stock finalization fills the partial staging page and calls `0x31B2` at `0x390C` to write the four-byte committed length to `0x08008A00`. The helper rebuilds and writes the whole page. It does not copy the staged application into its destination. |
| A0 | `0x10017B184` calls wrapper `0x10017A544`, using `02 A0` at `0x10084DF10`. | The caller then writes success at `0x10017B188..0x10017B190` without checking A0's return. The preceding A3 return is checked. Success on this path means its A3 check passed and it attempted A0. |

Selection routing is visible in code and Swift type metadata. Context
descriptor `0x10068F100` identifies `FirmwareListItem` with fields `id`,
`title`, and `source`. The source type reference resolves to `FirmwareSource`
at `0x10068F130`. Its cases are `bleLocal`, `eafRemote`, `bleRemote`, and
`unsupportedRemote`. At `0x1000D81F8` the caller loads the `source` field
offset. It extracts the enum tag through `swift_getEnumCaseMultiPayload`
at `0x1000D8218`. The `eafRemote` branch reaches `0x1000D82F0` and then
tail-calls the EAF download routine at `0x1001775F0` from `0x1000D8360`.
The unsupported branch at `0x1000D8364` builds the rejection message at
`0x1000D838C`. This establishes the selected-item route. It does not execute
the UI or prove the server's model list authentic.

The A1 wrapper's stream path is now traced beyond the earlier review's open
edge. At `0x100179CDC` it requests the session's `outputStream`, stores that
stream and the constructed data in closure state, and selects closure
`0x100181488` at `0x100179D20..0x100179D24`. The imported call at
`0x100179D34` is `DispatchQueue.sync(execute:)`. The closure calls
`0x10017ECCC` at `0x100181490`, which loads its arguments and calls the
production `SessionHandler` writer at `0x1001CB86C` from `0x10017ECDC`.
The writer calls `write:maxLength:` at `0x1001CBA2C`.
This is a static call and data-flow trace through the Swift runtime closure;
the original app was not executed.

The expected hash is tied to the selected `Firmware` record. Swift context
descriptor `0x10068F7B8` names `Firmware`; its field descriptor at
`0x100723294` lists `model`, `firmwareRevision`, `file`, and `sha256sum`, in
that order. At `0x10017C2C0` the caller obtains this type's metadata, then
uses its fourth field offset at `0x10017C2C4` to load `sha256sum`.
`0x10017C2F8..0x10017C310` passes that string into `0x10017A740`.
The latter stores its two string words in the context that the hash comparison
reads. This is stronger than finding a SHA-256 symbol or manifest key alone.
The descriptor's field-offset-vector word offset is 2. On this arm64 binary
the vector therefore starts at metadata byte offset 16, and its fourth
32-bit entry is at `16 + 3 * 4 = 0x1C`. This interpretation matches
Swift's [`TargetStructMetadata::getFieldOffsets`](https://github.com/swiftlang/swift/blob/main/include/swift/ABI/Metadata.h).

`SessionHandler` initialization at `0x1001CB820` calls `0x1001CBD64`, which
passes the supplied accessory and protocol to `EASession`. EAF service
initialization at `0x1001A6874..0x1001A6884` stores
`com.clickscompanion.protocol`. The plist also permits `com.clicks.protocl`.
The second name is not evidence of a separately running target service.
No executable use of that second protocol has been established here.

The stock A0 handler at `0x3954` sets context flag bit 1 and prepares
`04 02 A0 00`. The foreground call at `0x238E` enters `0x39FA`.
Its flag branch reaches `0x3A24..0x3A30`, writes `0xBEEF0080` to
PFIC_CFGR at `0xE000E048`, then spins at `0x3A32`. WCH RM section
7.5.2.10 defines the key and system-reset bit used here. The path does not
select `FLASH_STATR.BOOT_MODE` or unlock `BOOT_MODEKEYR`.

**The exact known-to-unknown control boundary is the hardware reset requested
at stock `0x3A30`, before the next reset-time instruction.** The supplied
application files start at `0x2000`; they do not include the code that selects
the application or consumes the staging marker after reset. Naming A0 a
"reboot into the installer" adds behavior that this path alone does not prove.

The traced native update caller returns after constructing its result. It
does not wait for a new accessory session or compare a post-reboot version
with the selected image before returning success. Session notifications and
version getters elsewhere in the app cannot be counted as this verification
without a binding call/data-flow edge. No such edge was established in this
operation. No physical reboot, disconnection, or reconnection was observed.

The inspected IPA contains no `.bin`, `.hex`, `.dfu`, `.fw`, or `.img` payload
or firmware/bootloader/recovery/ISP-named path. Other Bluetooth-related code
exists, but no model-matched, application-independent CK-5200 recovery target
was established through that route. Neither an unrelated product updater nor
a plist permission proves this keyboard has the required service.

### Reproduction and checks

Use the existing stock ELF wrapper and range printer for disassembly. The ELF
wrapper creates analysis containers from BINs; these are not original OEM ELFs.

```sh
python3 keyboards/ck5200/tools/stock_audit/mkelf.py <stock-v122.bin> /private/tmp/v122.elf
/opt/homebrew/opt/llvm/bin/llvm-objdump -d --triple=riscv32 --mattr=+m,+a,+c,+xwchc /private/tmp/v122.elf
python3 keyboards/ck5200/tools/stock_audit/installation_evidence.py <stock-directory> <qmk-v1.bin>
python3 ios/ClicksInspector/Tests/verify_official_app.py <decrypted-ipa>
python3 -B -m unittest discover -s keyboards/ck5200/recovery_lab -v
```

The new static evidence script passes on both pinned stock files and the frozen
QMK BIN. One-byte mutations in V122 and QMK each fail with exit 1. The existing
official-app byte verifier passes. All 10 existing recovery-laboratory tests
pass. Those tests retain their explicit counterfactual preservation policy;
passing them does not validate the OEM installer.

The expanded native verifier checks 14 ARM64 direct call targets, the four
`Firmware` fields, and the unchecked A0-result path. A negative control with
an incorrect expected A0 target fails with exit 1. This control tests the
call decoder rather than merely changing the pinned executable hash.

Independent review used the original disassembly, pinned input files, the two
evidence scripts, and the actual logs. It found a contradictory older readiness
sentence that still called the tail protected. Both readiness paragraphs now
identify that classification as policy. The reviewer independently reproduced
the startup math, A1 limit, QMK interval occupancy, deferred reset, and the
native caller's unchecked A0 result. The review record is retained with the
investigation receipts.

The full firmware build, firmware sanitizer suite, iOS product builds, and
simulator UI suites were not rerun because this change touches evidence tools
and documentation only. No native updater execution, original installer
execution, physical device operation, or interruption test was performed.

`riscv-none-elf-size`, `nm`, `readelf`, and `objdump` were run on the frozen ELF.
The existing image validator passes BIN/ELF identity and memory bounds. The
loadable application ends at `0x893C`; initialized data is 144 bytes, BSS is
4,680 bytes, retained RAM is 8 bytes, and the reserved stack is 2,048 bytes.
No firmware source, linker script, iOS controller, or build configuration changed.

The WCH RM PDF inspected here has SHA-256
`c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a`.
The datasheet has SHA-256
`1714c07d9448bc87ec667a602eba3e4c784a6ca7ca25ef125445df0ee4cc755d`.
The manual's archived direct-download bytes match the archived pinned copy.

Sources include the [WCH manual](https://www.wch-ic.com/download/file?id=382),
[WCH datasheet](https://www.wch-ic.com/download/file?id=381), and
[pinned flash driver](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/SRC/Peripheral/src/ch32x035_flash.c).

## Next

### Recovery candidates tested against the evidence

| Candidate | Evidence and falsifiable condition | Implementation decision |
| --- | --- | --- |
| OEM reset-time installer | A3 commits a length in staging; A0 requests a normal system reset. A qualifying loader must remain reachable after invalid application vectors and expose an iPhone-accessible service or an independent restoration trigger. Its marker validation, copy sequence, source retention, retry state, and bad-application fallback must be established. | No evidenced target service exists to bind a new client to. The existing application-level transfer implementation is already present. Implementing another sender would not supply the reset-time service. |
| WCH System FLASH loader | WCH documents PC17-high power-up entry and a separate launcher. Its actual presence on this keyboard, configuration, noninvasive entry, and protection state are not established. A qualifying route must also expose an interface the iPhone can access. | No generic WCH loader is assumed installed. No entry experiment, option change, or programmer action is authorized. |
| Existing QMK application rescue | Production `main.c` checks a two-word retained-RAM request, then initializes the application transport. | Already implemented. It cannot execute if its startup or required transport code is corrupt, so more host code cannot make this route application-independent. |
| The app's second declared EA protocol | The supplied app declares `com.clicks.protocl` as well as `com.clickscompanion.protocol`. A qualifying independent route would need a CK-5200 service that advertises and answers that protocol with the application unavailable. A plist declaration supplies neither a service nor entry behavior. | No speculative protocol or enablement is added. The protocol's purpose remains unverified until its selection and target are established. |
| External USB host adapter or replacement recovery loader | Could change the available host or target software, but requires additional hardware or installing new target code first. | Excluded by this task's hardware and physical-write restrictions. It cannot establish a recovery route already present on the keyboard. |

The factory-loader candidate has a separate host constraint. Fresh Apple
documentation JSON was fetched in this investigation. USBDriverKit's explicit
hardware note supports macOS and M-series iPads. It does not document iPhone
support. `AAUSBAccessory` lists macOS 27.0. DriverKit's generic metadata includes
an iOS badge, but its own hardware note restricts the supported mobile devices
to M-series iPads. An iOS badge alone is therefore insufficient evidence.
`EASession` is available on iPhone but requires an accessory-provided protocol.
It does not create raw endpoint access to an arbitrary factory USB device.
These API findings do not exclude a privileged OEM solution; no such solution
was established in the inspected artifacts.

Primary Apple sources are [USBDriverKit](https://developer.apple.com/documentation/usbdriverkit),
[DriverKit](https://developer.apple.com/documentation/driverkit),
[creating drivers for iPadOS](https://developer.apple.com/documentation/driverkit/creating-drivers-for-ipados),
and [EASession](https://developer.apple.com/documentation/externalaccessory/easession).
Their fetched JSON and hashes are retained with the local evidence.

### Exact remaining obstacle and evidence acquisition

The decisive missing component is executable or authoritative, model-matched
evidence for a reset-time restoration service that survives an unbootable
application and remains reachable from the iPhone. It must include its update
and preservation contract. The stock application's maximum length and native
app's success result cannot substitute for that component.

Two different installer implementations can satisfy every application-side
observation here while behaving differently after a power cut or invalid image.
For example, one can retain a valid staged source and retry copying, while
another can clear its marker before copying. This is a logical counterexample,
not an assertion that either implementation is installed. The inspected
application instructions do not select between them because neither includes
the code that consumes the marker after reset.

The strongest next technical action is to obtain an authentic CK-5200 hardware
1.2.0 reset/installer artifact, including the presumed main-flash prefix
`[0x0000,0x2000)` and any relevant System FLASH/configuration, or an equivalent
OEM recovery contract. That evidence must identify a target interface and
independent entry, not only promise normal updates. It would permit static
analysis and a target-bound software client before a physical experiment.

The current supplied application images and the inspected IPA do not provide
those bytes. The fresh public manifest fetch returned 403. No further analysis
of these same application bytes can prove the absent code's behavior. An OEM
artifact or model-matched documentation can change that evidence set. Vendor
contact remains outside the standing authorization. Acquiring the missing
bytes through a patched application would first require a firmware write.
Reading them through debug would require device probing and potentially
additional hardware. Those routes are excluded here.

If only physical validation can resolve the target behavior, it would require
a separately authorized, model-matched test of independent entry with an
unbootable application, iPhone protocol communication, stock restoration, and
interruption at erase/copy/commit boundaries. Such a test is destructive unless
an independent recovery method has already been secured. It is prohibited by
the current no-modification rule for the only keyboard.

No new recovery implementation is justified by the inspected target evidence.
The concrete software changes in this investigation are reproducible static
evidence checks and corrected ownership claims. No firmware layout change or
physical action follows from the ownership correction.
