# Memory ownership and the OEM update transition

Checked: 2026-10-20. Branch `feat/ck5200-tail-ownership-evidence`, based on `6a29277`.
Scope: static analysis of already-held artifacts only. No device access, firmware
command, reset, ISP operation, protection change, or vendor contact occurred.

This note supersedes the preservation conclusion of [v122-tail.md](v122-tail.md)
and supplies the missing evidence that review demanded. It answers three
questions: what the flash regions actually own, what the OEM update transition
provably does, and which recovery routes the new evidence supports.

## Reproduction anchors

Inputs (all previously archived; hashes re-verified during this pass):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `iKeyboard_CK-5200_V114_120.bin` | 16,828 | `23327377867f8c3e14168d5af25e18ef4cea697d7d0b203cf970da06d5f6ac8f` |
| `iKeyboard_CK-5200_V115_120.bin` | 17,452 | `336f5658631bd5a9db1aba7481534574e0725e3c583bbd08b2263a8b4e85964e` |
| `iKeyboard_CK-5200_V116_120.bin` | 17,460 | `73a8f71683aadbdd74ef157d2662f6cd1c33ceb09dadca04c6a426f1d26e3e95` |
| `iKeyboard_CK-5200_V117_120.bin` | 18,296 | `0ee35d494a6edf96183a5f4cb41e61230af9b1324da66b737bc5cc7cb5b3a58b` |
| `iKeyboard_CK-5200_V121_120.bin` | 18,572 | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |
| `iKeyboard_CK-5200_V122_120.bin` | 18,648 | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |
| QMK v1 candidate `12cfe69`, CI `ck5200-firmware` artifact of run `39180108400` | 26,940 | `969e6d71a177119e19dd0a93fffee4cfc63f2c1b46c01a9ccf01d56ac353694e` |
| `.stock/clicks-app-prod-release.apk` | 32,550,542 | `fc60df6c69f0fb142892b9a8b531c60fb439b8d7e37bbde01886eea296499740` |
| Readable iOS `Clicks` executable (`com.clickscompanion.app_3.0.2_und3fined` IPA, `cryptid 0`) | 9,771,792 | `d56a59180adad89542107924edc1cc046507f6b11730f912f2855a8c408f5c6b` |

The six stock files come from the already-captured OEM firmware distribution set
(`tmp/ck5200-installation-evidence-usf0t3wf/frontier/firmware-private/`); V121/V122
are byte-identical to the audited `.stock/` copies. The QMK v1 image is the
`ck5200-firmware` artifact of the passing CI run `39180108400` on the frozen
`feat/ck5200-qmk-v1` branch at `12cfe69`; its extent is `[0x2000,0x893C)`.
An earlier draft of this note mistakenly verified a 26,860-byte
(`eef27f62…`) pre-v1 assurance build from
`tmp/ck5200-installation-evidence-usf0t3wf/frontier/hosted-assurance/`; every
QMK number below was recomputed against the 26,940-byte artifact.

Fresh work performed for this note:

1. ELF-wrapped and disassembled **all six** stock images with Homebrew LLVM 23.1.3
   `--triple=riscv32 --mattr=+m,+a,+c,+xwchc` (scratch: `tmp/ck5200-tail-evidence/`).
2. Window-anchored `lui`+`addi` and `auipc`+`addi` address-reconstruction scans over
   all six disassemblies, plus a whole-image scan for flash-pointer-like words.
3. Bit-transition erase-necessity computation for every consecutive stock pair,
   for V122-over-every-earlier-version, for QMK-over-V122, and for V122-restore-over-QMK.
4. Byte-level V121→V122 diff around the application boundary.
5. Decompiled the OEM Android updater from the archived APK with jadx 1.5.6 and
   reconstructed the update call path from executable code (not strings).
6. Re-read CH32X035 RM V1.9 §20 (flash controller) and §7.5.2 (PFIC) to name every
   hardware operation the disassembly performs.
7. Re-verified the earlier finding that the *other* retained IPA
   (`com.clickscompanion.app_3.0.2_und3fined.ipa.ipa`) carries an unencrypted
   (`cryptid 0`) iOS `Clicks` executable, and reconciled this note's iOS claims
   with the earlier executable-level trace in
   [recovery-research.md](../../keyboards/ck5200/docs/recovery-research.md).
8. Re-ran the padding tool and all transition computations against the actual
   QMK v1 artifact after an earlier draft mistakenly used a pre-v1 build.

## 1. Corrected memory-ownership assessment

### 1.1 Flash map with evidence

| Region | Content | Evidence (linked addresses, all six versions checked) |
| --- | --- | --- |
| `[0x0000,0x2000)` | Unknown boot stage. Owns the hardware reset vector; must jump to `0x2000`. | Hardware rule (RM §20.1: reset entry at flash base). No version's image contains these bytes. |
| `[0x2000,0x8A00)` | Application image space, all of it. | V122 A1 handler `0x37A8`: accepts any length `<= 0x6A00` (the full span). All six images end inside it, at five different addresses. |
| `[0x8A00,0xF500)` | Staging/scratch: 4-byte length marker at `0x8A00`, image from `0x8A04`. | V122 A2/A3 staging writes (`0x3818`, `0x38EC`, `0x38FE`); staging writer guard `0x08008A00-0x0800F4FF` in our own port already matched this. |
| `[0xF500,0xF600)` | Reserved gap. | No references in any version. |
| `0x0800F600` | Device serial word (formatted to 10 decimal digits). | V122 serial formatter `0x2276`; same access in V114 (`0x28E6`), V115/116 (`0x28E8`), V117 (`0x2252`), V121 (`0x2272`). |
| `0x0800F700` | Device config record: 12 bytes, first byte `0x0C` (length/validity tag), defaults `0C 80 21 00 A7 00 00 00 88 13 58 02` on invalid content. | V122 loader `0x39A0-0x39EA` and saver `0x39FA` (partial-page write via `0x31B2`); equivalent code in all versions (V114 `0x34BC`/`0x3508`, V115/116 `0x359C`/`0x35EA`, V117 `0x3854`/`0x38A2`, V121 `0x3956`/`0x39C4`). |
| `[0xF700,0xF800)` | Rest of the config record (3 words). | Same instructions as above. |

**Device-specific storage is at `0xF600`/`0xF700`, at fixed addresses in the
reserved top of flash.** The serial and the settings record are the only
device-individual bytes the application reads or writes, and the config record
is self-validating with defaults. Nothing device-specific lives in the
application image area.

### 1.2 What the six-version extents and the update chain establish

| Version | Image extent | Final-page tail |
| --- | --- | ---: |
| V114 | `[0x2000,0x61BC)` | 68 |
| V115 | `[0x2000,0x642C)` | 212 |
| V116 | `[0x2000,0x6434)` | 204 |
| V117 | `[0x2000,0x6778)` | 136 |
| V121 | `[0x2000,0x688C)` | 116 |
| V122 | `[0x2000,0x68D8)` | 40 |

The image end moved with every release and each successor writes new image
bytes into its predecessor's tail:

- V122's bytes at `[0x688C,0x68D8)` (76 bytes of V121's 116-byte tail) are V122's
  own `.data` initialization image: the descriptor string-table words
  `0x20000044, 0x663C, 0x6670, 0x66A0, 0x65E4, 0x6600`, timing constants, and
  the attach template `FF 55 02 00 EE 10` at `0x68D0`.
- The startup copy loop in each version is bounded exactly at the image end
  (V122 `0x485C-0x4890`: copies from LMA `0x6880`; V121 `0x4812-0x4846`).
  Verified: no version copies or references data beyond its own extent.

**Erase-necessity proof.** CH32X035 programming only drives bits 1→0; only an
erase restores 1s (RM §20.3, CTLR `FTER`/`FTPG`, buffer-based page program).
For every consecutive official pair, writing the successor over the predecessor
*requires* erasing pages, including the predecessor's final page:

| Update | Predecessor final page | Differing bytes in that page | Bytes forcing erase (0→1) |
| --- | --- | ---: | ---: |
| V114→V115 | `0x6100` | 164 | 119 |
| V115→V116 | `0x6400` | 31 | 24 |
| V116→V117 | `0x6400` | 48 | 31 |
| V117→V121 | `0x6700` | 106 | 61 |
| V121→V122 | `0x6800` | 119 | 69 |

Therefore **every official update erases the full 256-byte page containing the
image end, including the entire tail `[end, page_end)`.** The OEM's own supported
update path does not preserve the prior tail content through an update; a
"must remain unchanged" invariant for those bytes cannot exist in the OEM flow.
What the page can contain after an update is only erase residue (`0xFF`) or bytes
the installer re-wrote from the staging image (see §1.4).

### 1.3 Does any version access data beyond its declared image?

No. Three independent scans over all six disassemblies:

1. **Absolute `lui`-constructed flash references** (window-anchored `lui`+`addi`
   and `lui`+load/store): every version references exactly three flash areas:
   staging base `0x08008A00` (only inside the A2/A3 staging write path),
   `0x0800F600` (serial), and `0x0800F700` (config). Zero references into
   `[0x2000,0x8A00)` as data, zero into any tail.
2. **PC-relative `auipc`+`addi` computed targets**: zero targets beyond the
   version's own image end in any version.
3. **Whole-image 4-byte pointer-like scan** (values in `[0x2000,0xF800)` and
   `0x08002000`-based): every hit is either a known in-image address (descriptor
   table, identify blob) or a false positive (instruction encodings such as
   `lui a5,0x6` = `000067b7`, or ASCII). Zero pointers into any tail or the
   upper reserved region.

### 1.4 The 40-byte region `[0x68D8,0x6900)`: corrected classification

Answers to the six required determinations:

1. **Application-owned, reserved, or device-specific?** It is ordinary unused
   application-area flash: inside the app span `[0x2000,0x8A00)` that A1 accepts
   as image space, never referenced as data by any stock version's code, and
   inside a page the OEM update path provably erases. It is *not* reserved and
   *not* device-specific storage; device data has a fixed, different home
   (§1.1).
2. **What V121→V122 growth establishes:** the boundary is just the end of the
   linker image (end of `.data` LMA). It moves with every release, and V122
   writes real image data through 76 bytes of V121's former tail. There is no
   fixed pocket at the end of the application area.
3. **Does the original application access data beyond its declared image?**
   No (§1.3, three scans, six versions). Its only out-of-image reads are the
   serial word and the config record, both in the reserved top region.
4. **Origin of the preservation requirement:** it was a conservative assumption
   introduced in `qmk-delivery-evidence.md` ("the unknown tail is not present in
   the file; any overwrite remains outside the authorized mutation range").
   No OEM evidence ever supported it. The OEM's own update flow contradicts a
   strict-preservation invariant (§1.2).
5. **Technically possible erase/program behaviors for the final page** (RM §20.3):
   fast page erase `FTER` (whole 256-byte page → all `0xFF`); buffered fast page
   program `FTPG`/`BUFLOAD`/`BUFRST` (256-byte page, 1→0 only); 32 KiB block
   erase `BER32` (covers only `[0x08000000,0x08008000)`, which includes the
   stock final page but also the boot stage, so it is unusable); whole-user-area
   erase `MER` (destroys everything). **No sub-page erase exists.** Any writer
   that preserves tail bytes must read the page before erasing and reprogram
   them afterwards (read-modify-write), exactly what the OEM application's own
   partial-write helper (`0x31B2`: reads 256 bytes, overlays, calls full-page
   writer `0x3174`) does for staging and config.
6. **Safer image layout:** a padding tool is implemented in
   `keyboards/ck5200/tools/pad_staging_image.py` (this branch). Padding the QMK
   image with `0xFF` to the full app-area end `0x8A00` removes the *copy-model*
   ambiguity only. Precisely scoped: **if** the boot stage accepts the staged
   session (length `0x6A00` passes the application's A1 check, and staging fit
   holds) **and** commits it with the same semantics as the OEM flow, then the
   destination content no longer depends on whether its copy is length-exact or
   page-rounded, because the staged session covers whole pages and explicit
   `0xFF` equals erase residue. Padding does **not** establish that the boot
   stage accepts a `0x6A00`-length image (its own validation rules are unknown),
   does not make the padded file byte-identical to the official artifact (its
   hash differs: the v1 image padded to `0x8A00` hashes
   `458472f6f65adbe77a395715f6334e5a86efb5c33d852f9e75f508f68725cb19`), and is
   not an installer compatibility guarantee. It is a host-side staging choice
   that is valid at every layer we can inspect: the linked firmware image is
   unchanged, the application never reads beyond its linked image end (proven
   in §1.3), and `0xFF` is the erased state.

**Corrected requirement.** The old rule ("the 40 bytes must remain unchanged")
described an impossible invariant: the OEM's own updates erase them. The
correct invariant is *functional irrelevance*: the tail may end up `0xFF`, or
4 bytes of stale staging content under one installer model (see §3.2), and
every stock version provably ignores both. This is a requirement correction
backed by OEM evidence, not a license to expand any mutation range: flash
writes remain unauthorized, and the boot stage `[0x0,0x2000)`, staging marker
semantics, serial page, and config page remain untouched by every analyzed
path.

## 2. The OEM update transition, reconstructed from executable code

### 2.1 Host side: OEM Android updater (DEX, jadx 1.5.6)

Two retained Clicks 3.0.2 packages exist. The `com.clickscompanion.app_6466761655_3.0.2`
IPA (and its app extension) is App Store encrypted (`LC_ENCRYPTION_INFO_64`,
`cryptid 1`) and not statically readable. The `com.clickscompanion.app_3.0.2_und3fined`
IPA carries an unencrypted (`cryptid 0`) `Clicks` executable
(SHA-256 `d56a5918…`, re-verified in this pass), which the earlier investigation
[recovery-research.md](../../keyboards/ck5200/docs/recovery-research.md) already
traced at instruction level: the manifest URLs `firmwares.json` and
`firmwares-p.json`, `EAFService.loadFirmwares`/`downloadFirmware`, and the
`EASession` stream-write path (accessor `0x100178698`, writer `0x1001CB86C`,
`write:maxLength:` at `0x1001CBA2C`). That trace explicitly left the update-packet
construction and the CK-5200 model selection unresolved, and this pass did not
resolve the packet construction in the iOS binary either (no `0xA1`/`0xA2`/
`0xA3`/`0xA0` immediate materialization was found; the Swift encoder likely
builds frames through non-greppable paths). The Android app implements the same
update protocol over the same device commands, and its DEX supplies the missing
pieces; everything below is therefore reconstructed from executable code, and
the device-side disassembly independently confirms every frame.

Call path, with decompiled class names:

| Step | Code | Behavior |
| --- | --- | --- |
| Manifest | `p002a1.b.m` | Fetches the firmware list, keeps entries whose `model` equals the connected device's model, keeps `firmwareRevision > current`, picks the highest. Bean `CKFirmware`: `model`, `firmwareRevision`, `file`, `sha256sum`. |
| Download | `p002a1.o.m` (state `Q0.b DownloadFile`) | HTTP GET `fileUrl` to temp file. |
| Image validation | `p002a1.o.m` (state `Q0.a CheckSHA256`) | SHA-256 of the downloaded file compared to the manifest `sha256sum`. On mismatch it aborts ("file not match") before any device command. |
| Load | `p002a1.o.m` (state `Q0.e LoadFile`) | Reads the whole file into memory. |
| Transfer | `p002a1.s.j` states `StartUpdate`, `StartWrite`, `FinishUpdate` | `T0.o.b(len)`, `T0.o.c(bytes)`, `T0.o.a(len)` (below). |
| Reboot | `p002a1.s.j` state `Reboot` | Sends `02 A0`; declares `Success` when transport write `L0.a.f` returns true. No A0 reply read follows. |
| Sideload path | `p002a1.q.m` | Loads an image from app assets and runs the same A1/A2/A3/A0 with **no** SHA-256 check. |
| Post-reboot verification | none in the driver | Success is the A0 transport write result, not installation proof. No second-stage, boot, or recovery protocol was found in the app (searched classes, strings, broadcast actions: only `APP_ACCESSORY_CONNECT/DISCONNECT`, `ACTION_FIRMWARE_UPDATE_SUCCESSFUL/FAILED`, and settings events). |

The Android transport (`L0.a`) enumerates VID `0x352E` PID `0x2306`, claims
**interface 1** (the `FF/F0/01` companion-data interface), and exchanges frames
on its two bulk endpoints with 1000 ms timeouts. On Android no iAP2 or MFi
handshake precedes the update commands.

### 2.2 Wire protocol (OEM host code cross-checked against V122 disassembly)

Frame format is `[total_length, command, args...]` (big-endian multi-byte args).
The stock dispatcher (`0x3216`) reads byte 0 as the declared frame length
(`bltu a2,s3` rejects short frames), byte 1 as the command, and validates
`args == length-2` for the get/set families.

| Command | Host frame (OEM Android, `T0.o`) | Device handler (V122) | Device response |
| --- | --- | --- | --- |
| A1 begin | `[06, A1, len_be32]` | `0x37A8`: `len <= 0x6A00`; init cursor=4, first staging word `0xFFFFFFFF`, page offset 0 | `[08, 02, A1, 00, 00,00,6A,00]` (echoes max size 0x6A00) |
| A2 data | `[0x26, A2, offset_be32, data[32]]` per 32-byte chunk | `0x380A`: rejects any offset != staging cursor; buffers; fast-erase+fast-programs each full staging page at `0x08008A00+` | `[08, 02, A2, 00, next_offset_be32]` |
| A3 commit | `[06, A3, total_be32]` | `0x38DE`: requires `total ==` A1 length `==` received cursor; fills the partial staging page with `0xFF`; writes the 4-byte length marker at `0x08008A00` via the read-modify-write helper | `[08, 02, A3, 00, total_be32]` |
| A0 reboot | `[02, A0]` | `0x3954`: sets bit 1 of the state byte at ctx+0, replies first | `[04, 02, A0, 00]` then reset |

The A2 offset echo is a resync: the OEM host uses the response's 4-byte
`next_offset` as the next frame's offset field (read from response bytes 4..8 in
`T0.o.c`). Both sides advance only on an exact match, so a lost or reordered
exchange cannot desynchronize the staged image.

Our independently reconstructed ClicksInspector frames
(`CustomKeyboardProtocol.swift`) are byte-identical to the OEM Android frames:
`[06,A1,size_be32]`, `[6+chunk,A2,offset_be32,chunk<=32]`, `[06,A3,size_be32]`,
`[02,A0]`, A1 response payload `== 0x6A00`, chunk ACK `== offset+32`. Two
independent reconstructions (ours from device disassembly, theirs from their own
product code) agree on every byte.

### 2.3 Device-side control flow and the exact unknown boundary

- A3's marker write is the **only** commit action the application performs. The
  marker slot is initialized to `0xFFFFFFFF` by A1 and only receives the real
  length at A3, via a page read-modify-write. An interrupted staging session
  therefore leaves an invalid marker, which any sane consumer must ignore.
- The application **never reads** the marker: the only `0x08008A00` references
  are the three staging write sites. Nothing in the app consumes the staged image.
- The complete flash-write call inventory of both V121 and V122 is five sites:
  three full-page and two partial-page writes (staging pages and the config
  record). **No code path writes the destination application area.**
- The A0 path performs **no flash operation at all**: the main loop
  (`0x235E`) polls the state byte every iteration (`jal 0x39fa`); bit 0 flushes
  the config record to `0xF700`; bit 1 executes the reset at `0x3a20-0x3a32`:
  writes `0xBEEF0080` to `R32_PFIC_CFGR` (`0xE000E048`, KEY3=0xBEEF + SYSRST
  bit 7, RM §7.5.2) and spins. The A0 reply is sent before this runs.
- **The unknown begins exactly at that PFIC write.** After the system reset the
  hardware enters at `0x08000000`, inside the unread boot stage `[0x0,0x2000)`,
  which owns the reset vector and must transfer control to `0x2000`. Since the
  application demonstrably performs no destination copy and never consumes the
  marker, the boot stage is the only possible consumer: it must read the marker
  at `0x8A00`, copy the staged image to `[0x2000, 0x2000+len)`, and clear or
  invalidate the marker. Everything about that consumer, its validation, copy
  order, tail handling, interruption behavior, and whether it ever exposes a
  USB interface, remains unknown: those bytes were never read.

### 2.4 Session closure and post-reboot verification

The OEM Android driver declares success at the A0 transport write result.
[CORRECTION 2026-10-23] Re-reading `p002a1.s.j` and `L0.a.f` confirms that no
A0 acknowledgement read follows. The earlier ACK claim was incorrect.
The re-attach path is the ordinary
accessory connection flow: on the next connect the host reads identity/version
(the stock identify payload `01 20 01 22` = hardware 1.2.0, firmware 1.2.2) and
`p002a1.b` compares `firmwareRevision > current` for future updates. No evidence
exists of a boot-stage USB service, a recovery handshake, or any protocol beyond
the four commands: the iOS app declares only `com.clickscompanion.protocol` and
`com.clicks.protocl` in `UISupportedExternalAccessoryProtocols`, and the Android
app contains no other protocol surface.

## 3. Recovery routes with the corrected evidence

### 3.1 What the update chain now proves, and what it cannot, about interruption

The staging+marker design is a persistent, application-independent update queue:

- Staging failures and power loss during A1/A2/A3 only affect `[0x8A00,0xF500)`;
  the running application is untouched and can retry.
- A power loss between erase and program inside any staging page write leaves
  `0xFF` (invalid marker or corrupted staged image) — the boot stage must ignore
  an invalid marker, and a corrupted staging session can be re-run from the app.
- A power loss after a valid A3 marker but during the boot stage's destination
  copy leaves the marker valid and the staged image intact **only if** the boot
  stage consumes the marker in the retry-friendly order.

**[INFERENCE, not established]** The boot stage re-reads the marker on every boot
and re-commits a staged image after an interrupted copy. Supporting arguments:
the keyboard is USB-powered, so unplugging the iPhone mid-update is an ordinary
user event a shipping product must survive; and the marker's erased-state default
(`0xFFFFFFFF`, an impossible length) is exactly the idempotent retry contract
such a design requires. **Falsified-by-construction counterexamples that this
hypothesis cannot exclude**, because the boot stage's code is absent:

1. The boot stage may clear or rewrite the marker *before* copying. Power loss
   during the copy then leaves no valid marker and a partially written
   application, with no known re-entry.
2. The boot stage may validate the staged image against a manifest, checksum,
   or version window that an unauthorized or padded image fails, and may clear
   the marker on rejection.
3. The boot stage may have no retry at all; the staging architecture would then
   protect only against host-side transfer faults, not commit faults.

**Retraction.** An earlier report of this investigation proposed, as a "safe
observation", staging the current stock image with A1/A2/A3 and power-cycling
in place of A0 to observe marker retry. That proposal was wrong and is
retracted: A3 writes the valid installation marker, so power-cycling arms the
same boot-stage installation path that A0 would, over the same application
area. It is a firmware write experiment, not a read-only observation; seeing the
keyboard boot stock afterwards would prove only that the boot stage commits a
complete staged session, not that it recovers from an interrupted copy; and
under the marker-clearing counterexample above it could leave the only keyboard
with an invalid marker and a partially copied application. Do not execute it.

The equally important failure mode is **not** interruption at all: a QMK image
that installs completely but cannot start its USB/iAP2 stack. If the boot stage
consumed the marker on the successful commit, retrying the (possibly still
staged) QMK image cannot return the keyboard to stock, and no iPhone-reachable
service is known to exist when the application cannot run. Interrupted-copy
retry and unbootable-complete-install are therefore both unbounded risks until
the boot stage's behavior is known.

### 3.2 Destination outcomes for the two transitions, per installer model

Installing the QMK v1 candidate (`[0x2000,0x893C)`, 26,940 bytes) over stock
V122: 106 pages written, **all 106 require erase** (first page has 77 forcing
bytes; the stock final page `[0x6800,0x6900)` has 251 of 256 bytes differing,
153 forcing). The 40-byte tail is erased and rewritten with QMK image bytes
under every copy model. This is the same operation class the OEM's own updates
perform on that page. The QMK image's own final page `[0x8900,0x8A00)` supplies
60 bytes and leaves a 196-byte tail, which the padding tool replaces with
explicit `0xFF`.

Restoring stock V122 over QMK: 73 pages `[0x2000,0x68D8)`. Final-page tail
outcomes:

| Installer copy model | Tail `[0x68D8,0x6900)` after restore |
| --- | --- |
| Copies exactly `length` bytes | `0xFF` (erase residue) |
| Copies whole pages from staging (marker-shifted) | 36×`0xFF` + 4 bytes of stale staging content, which after a padded-QMK session are QMK v1 image bytes at file offsets 18684..18688 (`8700b296` for this artifact) |
| Read-modify-writes the destination final page | the destination tail preserved (QMK bytes) |

In every row the restored device runs stock from `[0x2000,0x68D8)`, and the tail
bytes are provably never read by any stock version (§1.3). The next OTA update
erases the page regardless. Padding the staged stock image to `0x6900` (the
tool supports `--pad-to`) collapses all three rows to explicit `0xFF`, at the
cost of deviating from the OEM's own staging length; the note's recommendation
is to keep the stock session byte-identical to the OEM session and accept the
cosmetic model-(b) residue, since it is functionally unobservable.

### 3.3 Route inventory

| Candidate | Status with this evidence |
| --- | --- |
| Boot stage re-commits a retained staged image | **[INFERENCE] Supported by architecture, unproven.** Would make mid-commit power loss recoverable on the next power-up. Requires the boot stage's code (vendor image or readback) or an authorized physical test. |
| Boot stage exposes its own USB/iAP2 recovery service | **No evidence.** No host software references one; the boot stage is 8 KiB and the app's iAP2+MFi stack alone exceeds that scale. Treat as absent until the boot stage is read. |
| WCH factory ISP (system FLASH launcher, PC17 entry) | Unchanged: mutable system FLASH, programs from zero, no preservation contract, no iPhone raw-USB API. Rejected previously; nothing here changes it. |
| Application-level rescue (M2) | Unchanged: real and implemented, but not application-independent. |
| Generic CH32X035 DFU/HID replacement loaders | Unchanged: would replace the boot arrangement before recovery exists; excluded. |

**Concrete falsifiable conclusion for R3:** no software-only evidence available
under current authorization can establish application-independent recovery. The
exact missing component is the boot stage's behavior after reset: (a) whether it
re-validates the `0x8A00` marker on every boot and re-commits a staged image
after an interrupted commit, and (b) whether it exposes any interface when the
application cannot run. Three existing restrictions block every acquisition
route: no flash readback or SDI probing of `[0x0,0x2000)` (physical/authorization
ban), no firmware write or reset experiment on the only keyboard (M1 ban), and
the vendor inquiry remains unsent by standing decision. The strongest technical
action available is to send the prepared vendor inquiry with §2.3's questions,
which are now precise: marker validation and retry semantics, copy granularity
(`length`-exact vs page-rounded), final-page handling, and marker clearing.

### 3.4 Net effect on the installation decision

The 40-byte tail, previously the largest concrete unknown, is now classified
with OEM evidence: it is unused application-area flash whose content no stock
code reads and whose page every official update erases. This removes the
"we might destroy unknown required data" objection. It does **not** demonstrate
safe installation. What remains unbounded, in the order the install decision
must weigh them:

1. **Boot-stage commit semantics are unknown in both directions.** Whether it
   accepts a `0x6A00`-length (padded) session at all, whether it validates the
   image, whether it clears the marker before or after copying, and whether it
   retries after an interrupted copy are all open (§3.1, including the
   counterexamples).
2. **A completed but unbootable QMK installation has no demonstrated
   recovery.** If the boot stage consumed the marker on the successful commit,
   no known iPhone-reachable service exists while the application cannot run,
   and staging is not itself a restore path unless the application is alive to
   stage one. The padding tool and the retry hypothesis do not address this.
3. **The QMK image has never booted on physical hardware.** Software-model
   assurance covers a running-but-degraded app (M2 rescue); it cannot cover a
   dead one, and it cannot bound risk 2.

These are decision inputs for the install authorization, not open questions
this or any held artifact can close without the boot stage's implementation.
[flash-readiness.md](../../keyboards/ck5200/docs/flash-readiness.md)
remains the sole authorization authority; this note changes evidence, not
authorization.

## Corrections to prior records

1. **[CORRECTION] to [v122-tail.md](v122-tail.md):** "the bytes' preservation
   remains required" is superseded. The OEM's own V121→V122 update provably
   erases the full final page (69 forcing bytes in `[0x6800,0x6900)`), V122
   writes image data through V121's former tail, no version reads any tail, and
   device data lives at `0xF600`/`0xF700`. The unknown tail is ordinary unused
   application-area flash. The page-padded layout removes the remaining
   installer-model ambiguity for the QMK side, conditional on boot-stage
   acceptance.
2. **[CORRECTION] to the ADR's architecture table**
   (`0001-independent-iphone-recovery.md`, §2 "Existing installer retries a
   retained staged stock image"): this remains a hypothesis, now with the
   marker's erased-state default and the USB-powered interruption argument as
   supporting evidence, and now with its falsifying orderings stated
   explicitly (§3.1). Still unverified.
3. **[CORRECTION] to this note's own first draft (2026-10-20):**
   - It verified a 26,860-byte pre-v1 assurance build (`eef27f62…`) instead of
     the actual QMK v1 artifact (26,940 bytes, `969e6d71…`, extent
     `[0x2000,0x893C)`); all QMK numbers were recomputed and the branch was
     rebased onto `12cfe69`.
   - It stated that destination content is identical "under every possible
     installer copy model" without the acceptance precondition; the claim is
     now scoped (§1.4.6).
   - It proposed a stock-staging power-cycle "observation"; retracted as a
     firmware write experiment (§3.1).
   - It treated "iOS executable analysis unavailable" as a property of both
     retained packages; one package is unencrypted and already traced at
     instruction level (§2.1).
   - It framed mid-commit interruption as the only unbounded install risk;
     a completed-but-unbootable installation is equally unbounded (§3.4).
4. Confirmed (no correction needed): the earlier reading of the staging
   mechanics, the `0x31B2`/`0x3174` helpers, the `0x6A00` size contract, and the
   `0x8A00`/`0xF500` staging bounds all match this pass's independent
   disassembly.
