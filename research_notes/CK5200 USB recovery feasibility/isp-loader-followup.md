# R3 follow-up: factory ISP erase and program boundaries

**Outcome 2: exact vendor dependency. No preservation-safe factory-USB strategy is established.**

The compiled WCH host library supports an address field in A5 PROGRAM. Its A4 ERASE routine sends only a count. `IAPStartAddr` does not supply an erase start for device type `0x23`. `IsEraseAllCFlash=false` reduces the count but does not disable erase. These are verified host facts, not proof that the target lacks another command or implicit page erase.

Investigation date: 2026-10-17. This note supplements [isp-preservation.md](isp-preservation.md). It independently checks the official compiled library, archive contents, PDFs, and EVT source. It does not use the earlier report as proof of target behavior.

## Scope and evidence rules

- Static analysis only. No WCH executable or library was loaded, linked into a test, emulated, or executed.
- No device access, enumeration, reset, USB/ISP command, or vendor message occurred. Network requests retrieved public source material only.
- The only durable output is this note. Scratch material is under `tmp/r3-isp-followup/`. No commit or push occurred.
- Addresses in host findings are ELF virtual addresses, not MCU addresses. Main-flash ranges below are offsets from `0x08000000`.
- `0x23` means the ISP device-family byte. It is not the individual package ID or `wchisp`'s separate MCU-series value.
- An absent host feature does not prove an absent target feature. Target behavior stays unknown without a matching loader implementation or vendor contract.

## 1. Verified official-host behavior

The primary binary is **H**, defined in the source register below. It retains named symbols. LLVM disassembly and ELF relocations identify the relevant routines and globals.

### A4 is count-only in this implementation

`CH55xIsp_Erase`, **H `0x5f31..0x61a1`**, receives a device index and one count argument.

- At `0x6001..0x6045`, device type `0x10` has a minimum count of four. Other types, including `0x23`, have a minimum of eight.
- At `0x6045..0x60a7`, the routine sets opcode A4, payload length four, and copies the low 32 bits of the count in little-endian order.
- At `0x60ac..0x60df`, it sends **seven total bytes**. There is no start field, appended address, or type-`0x23` packet extension in this routine.
- At `0x6155..0x6172`, it accepts a zero 16-bit returned result. This is stricter than treating any framed reply as success.

**Limit:** The binary proves the transmitted count and minimum. It does not prove the target's erase origin, unit, rounding, minimum, or physical effect. A zero result is not a preservation guarantee.

### `IsEraseAllCFlash` chooses the count, not a range

In `WCH55x_FlashProgramB`, **H `0x1ae3e..0x1aeab`**:

| Setting | Count passed to A4 |
|---|---|
| `IsEraseAllCFlash != 0` | `max(8, FlashMaxSize >> 10)` |
| `IsEraseAllCFlash == 0` | `max(8, (prepared_length + 1023) >> 10)` |

The `0x23` branch sets `FlashMaxSize=0xf800` at **`0x19f60..0x19f80`**. Thus the first case requests 62 units. The second case requests 19 units for an 18,648-byte BIN. This calculation describes host output only.

The erase call remains present in both cases. The separate `IsOnlyVerify` global bypasses the erase/program block at `0x1adfe..0x1ae09`. It is not a program-without-erase option. `WCH55x_FlashVerifyB` sets this global and calls the shared routine at `0x1b98a..0x1b9ce`.

**[INFERENCE]** The host treats these units as 1 KiB because it divides byte lengths by 1024. A prefix-erase interpretation fits its zero-based program loop. It does not establish the target's footprint independently.

### `IAPStartAddr` is not an X035 app-base control

The packed Linux x64 header layout gives:

| Field | Offset in `CH55xIspOptionS` |
|---|---|
| `IsEnableIAP` | `0x3c` |
| `IAPStartAddr` | `0x40` |
| `IsEraseAllCFlash` | `0x361` |

The layout uses eight-byte `ULONG`, four-byte `BOOL`, and `#pragma pack(1)`. The resulting structure size, `0x3cc`, matches the `AfxIspOption` symbol. Relocation `0x26f50` identifies that global. These offsets must not be copied to a Windows ABI.

`CH55xIsp_CreateChipConfigBit` reads `IAPStartAddr` in other-family configuration branches. Examples are **H `0x7683..0x76d3`**, `0x7c71..0x7cc1`, `0x8295..0x830a`, and `0xa6b4..0xa729`. They encode IAP configuration bytes, not an A4 start address.

The **type-`0x23` branch at `0x9fc6..0xa3cf` does not read `IAPStartAddr` or `IsEnableIAP`**. It constructs read-protection, user-option, data, and write-protection bytes, then exits that branch. In particular, it sets the RDPR pair from `IsCodeProtect` at `0xa015..0xa04a` and copies `WPR_DATA` fields at `0xa237..0xa29b`.

The public `WCH55x_FlashProgramB` signature also accepts a separate IAP image. In H, its IAP pointer, length, and format arguments are only forwarded on the network-interface branch at `0x199d9..0x19a22`. They do not provide a second, offset USB write path in the inspected function.

**Conclusion:** Setting `IAPStartAddr=0x2000` cannot be justified as an X035 erase-start or USB program-base setting in this library.

### A5 accepts a host address, but the high-level loop starts at zero

`CH55xIsp_Program`, **H `0x5caa..0x5f30`**, serializes its address argument into four little-endian bytes at `0x5d87..0x5da4`. It adds a random padding byte and encrypted data. Its payload length is `5 + data_length`.

This low-level routine has no code-flash range check and no call to `CH55xIsp_Erase`. It sends A5 and checks a zero 16-bit result at `0x5ee4..0x5f01`.

The higher-level USB path behaves differently:

1. It reads the user image with base argument zero at **`0x1a5a9..0x1a5da`**.
2. It sets the program offset to zero at **`0x1b09f`**.
3. It passes that offset directly to A5 at **`0x1b161..0x1b18e`**, then advances by the transmitted length.
4. For configured family `0x23`, it sends an empty final A5 at the end offset. The family comparison is at **`0x1b200..0x1b20d`**, and the call is at **`0x1b287..0x1b2a2`**.
5. The normal A6 loop also starts at zero, at **`0x1b3fb`**, and passes its offset at **`0x1b515..0x1b542`**.

`ReadDataFromDnFileB`, **H `0x113e5..0x115a8`**, copies BIN data and rounds its length to eight bytes. Its normal padding value is `0xff`. The supplied 18,648-byte stock length already meets this alignment.

**Host fact:** A custom serializer can express a nonzero A5 offset. The official low-level routine does this without a separate host erase call.

**Target unknown:** Whether the X035 factory loader accepts an initial offset of `0x2000`, requires prior A4, erases during A5, or modifies neighboring bytes. No host disassembly can answer those questions by itself. The empty final A5 and A6 effects also remain unknown.

### Type `0x23` does not bypass protection checks

At **H `0x1a25c..0x1a3a6`**, the shared programming path includes families above `0x22`. For normal programming, it requires the returned RDPR byte to equal `0xa5` and four protection bytes to equal `0xff`. Otherwise it exits with a protection error. `IsOnlyVerify` takes a different branch.

The same normal flow calls `CH55xIsp_CreateChipConfigBit`, then `CH55xIsp_WriteConfig` before A4, at **`0x1a6c4..0x1a6ec`** and **`0x1a805..0x1a83d`**. Therefore, the public programming function is not an option-preserving, offset-only primitive.

This host check does not establish the keyboard's actual RDPR/WPR state. Nor does the verify exception establish safe protected-state comparison on the target.

## 2. Target evidence and factory BOOT search

### What the MCU manual establishes

The independently re-read **RM V1.9, printed p.237, §§20.1–20.2.2** establishes 256-byte main-flash pages and 2-KiB write-protection groups. It lists the separate 3,328-byte System FLASH launcher at `0x1fff0000..0x1fff0cff`.

The same page prohibits protected main-memory erase/program from System FLASH, debug, and RAM, except whole-chip erase. **Printed p.249, §20.5.4** states that releasing read protection causes an entire flash erase. This is a genuine hardware restriction, not a client limitation.

The manual describes hardware flash operations. It does **not** specify the factory loader's A4/A5 dispatch, buffering, status timing, or interruption behavior.

### Search result: no matching factory-loader implementation recovered

| Inspected distribution | Result and limit |
|---|---|
| Official `WCHISPTool_CMD.ZIP`, 103 members | Host wrapper source, headers, drivers, and compiled host libraries. No separately packaged X035 factory BOOT image or target-loader source found. |
| Official `WCHISPTool_Setup.exe`, 66 extracted/listed NSIS files | Host applications, DLLs, drivers, configuration, and manuals. No separately packaged X035 factory BOOT image found. The installer was read as an archive, never run. |
| Official `openwch/ch32x035`, pinned below | Optional IAP and `FLASH/BootAsUser` examples. The `.BIN` files found are PIOC examples, not the factory USB loader. |
| Current Chinese WCH-LinkUtility download | Metadata advertises version 3.1, dated 2026-09-09. Download API returned `请刷新页面后再重试!`. The current archive was not obtained. |
| Pinned WeAct mirror of `WCH-LinkUtility .ZIP` | Contains probe firmware under `Firmware_Link/`, host EXE/DLLs, and manuals. No standalone X035 factory BOOT image. This is a third-party mirror, not proof of current WCH distribution contents. |

No identified factory BOOT payload was recovered from host executable resources. This bounded search does not exclude an embedded, compressed, or separately distributed image. No target disassembly is claimed.

The official **`BOOT区域作为用户区使用说明.pdf` V1.2**, pp.2–4, explains how user code can occupy System FLASH. It states that this replaces existing BOOT code and uses WCH-LinkUtility. It supplies neither the original X035 factory BOOT image nor a factory-USB erase protocol.

The optional EVT USB/UART IAP is a different implementation:

- `iap.h:20–35` selects app base `0x08005000`, opcodes `0x80..0x84`, and `CalAddr=0x0800f800-4`.
- `iap.c:64–99` only unlocks on its ERASE command, then erases pages during PROGRAM and the first VERIFY flush.
- `iap.c:116–123` erases the page containing `CalAddr`, main-flash offset `0xf700`, on END.

This EVT IAP source proves implicit erase only for the optional sample, not factory A5. It also violates this keyboard's supplied preservation constraints if used unchanged.

The retrieved host manuals describe programming controls, not a target wire contract. `WCHISPStudio_EN.pdf` p.9 and `WCHISPTool_CH32VXXX_EN.pdf` p.7 show `IsEraseAllCFlash=0` in configuration examples. Neither example defines a selective erase start.

## 3. Exact vendor dependency

**[INFERENCE]** R3 needs a version-specific target contract before an offset-programming strategy can become admissible. These questions should go to WCH and the keyboard vendor. No message was sent.

1. **Loader identity and artifact:** Which X035 package, chip revision, factory BOOT version, and BOOT hash does this keyboard use? Supply the matching System FLASH image or reviewable A4/A5/A6 implementation. Distinguish factory BOOT from optional EVT IAP and probe firmware.
2. **Selective erase:** Does that factory USB loader support an erase start, page list, or range? Give the exact opcode, payload, address space, alignment, and supported versions. For A4's four-byte count, define the unit, origin, minimum, upper bound, rounding, and any state-dependent interpretation. Does any documented A4 extension exist?
3. **Offset programming and implicit erase:** Can the first A5 address be main offset `0x2000` without prior A4? State every page erased or programmed by A5. Include partial packets, page crossings, address gaps, retries, empty final A5, first A6, A2, and disconnect cleanup. Does any step use read-modify-write or require erased flash?
4. **Preservation footprint:** Can the sequence leave `[0,0x2000)`, `[0xf600,0xf800)`, and all other unauthorized bytes untouched, including transient erase? Include System FLASH, option bytes, factory configuration, and external authentication state. Confirm whether any configuration transaction is required.
5. **Protection:** With the keyboard's actual RDPR/WPR values, can this sequence operate without releasing protection or changing options? Does encountering a protected group reject the entire operation before mutation, skip it, or permit partial mutation elsewhere? A destructive unlock is not acceptable.
6. **Last page:** The supplied stock image ends at `0x68d8`. What is the required content and ownership of `[0x68d8,0x6900)`? Page erase must not silently replace those 40 unknown bytes.
7. **Power loss and recovery:** For each command and flush, specify the possible physical footprint after brownout, reset, timeout, or USB loss. Does ACK mean durable completion? Are retries idempotent? Can interruption affect adjacent/protected pages or options? Can this exact keyboard re-enter intact factory ISP with a damaged application and no application command?
8. **Status and validation:** Supply operation-specific status meanings and a vendor-approved validation procedure on expendable matching hardware. Include a way to establish preservation of unknown data. A compare of supplied application bytes cannot establish that preservation.

## 4. Bounded decision

- **Disproven host path:** `IAPStartAddr=0x2000` plus `IsEraseAllCFlash=false` is not a selective-erase solution in H. The X035 flow still constructs options, sends count-only A4, and programs from zero.
- **Unresolved target path:** Direct nonzero A5 with page-local implicit erase, or a documented alternate erase command, remains a vendor question. This investigation does not prove either possible or impossible.
- **Current result:** Outcome **2**, exact vendor dependency. Outcome **1**, a safe strategy, lacks target-side evidence. Outcome **3**, a universal disproof of factory-USB restoration, is not justified.

Checks completed: binary hashes, symbol/relocation inspection, static control-flow review, archive member lists, PDF extraction, pinned EVT source review, and range arithmetic. Hardware validation and loader execution were excluded. The current official WCH-LinkUtility archive remained unavailable. A mirrored Link manual required an additional PDF crypto dependency and was not used as evidence. No repository test applies to this research-only note.

## Source register and reproduction anchors

**H: official compiled host library**

- Download: <https://www.wch-ic.com/download/file?id=365>.
- Archive SHA-256: `2da0b385bc789ab9cd468dd6dc29ec117a98dd00ad92e5ad5c9fb5b66868c4bd`.
- Member: `WCHISPTool_CMD/Linux/lib/x64/dynamic/libwch55xisp-4.0.0.so`.
- Library SHA-256: `b73d34967fd13e4f1f71749019666d826601e16c48ee02068196add964cc1bf8`.
- Adjacent `WCH55XISPDLL.H` SHA-256: `b333152ff7a679e4bb170fddcf9fa107bf82750bf8cc55cada5615e6f6541ce6`.
- Adjacent `LinuxTypes.H` SHA-256: `492611caa42b4b07d32b0eac7088dfbbc4179b379cd80db79017b97aca4c2d30`.
- Header anchors: `WCH55XISPDLL.H:73,109–173,332–348` and `LinuxTypes.H:37,54,57,72`.
- Local input prefix: `tmp/r3-isp/official-isp/`. Static outputs: `tmp/r3-isp-followup/{symbols.txt,relocations.txt,host-disassembly.txt}`.
- Analyzer: `/opt/homebrew/opt/llvm/bin/llvm-objdump`, with `--syms --demangle`, `--dynamic-reloc`, and `--disassemble --demangle --no-show-raw-insn --x86-asm-syntax=intel`. These commands read H as data.
- Relocation anchors: `0x26f50=AfxIspOption`, `0x26fa0=IsOnlyVerify`, `0x26fe0=gCurrentChip`. Symbol addresses and instruction ranges above identify the analysis without publishing binary payloads or disassembly dumps.

**Official Windows host package and manuals**

- Download: <https://www.wch-ic.com/download/file?id=196>.
- `WCHISPTool_Setup.exe` SHA-256: `8d2004251dcb59d43c63a132f341a76dca99b863fc03c1556987b679e8afac95`.
- `Doc/WCHISPStudio_EN.pdf` SHA-256: `db5faf27864037ae2723df69ccccd4be84019841ac123bb65c06e71e612fedec`.
- `WCHISPTool_CH32Vxxx/Doc/WCHISPTool_CH32VXXX_EN.pdf` SHA-256: `8e2061742624bd2d8aa83bf3d32cfc13c485d021ec959a84d6444622514451b3`.
- Static extraction used 7-Zip 26.04 from its official release. No installer or extracted WCH program ran.
- Metadata says version 3.6, dated 2023-10-18, but the archive lists 2026 members. Use the hash, not that metadata, to identify this inspected payload.

**Official MCU references**

- [CH32X035RM.PDF V1.9](https://www.wch-ic.com/download/file?id=382), SHA-256 `c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a`. Printed pp.237,249 equal PDF pp.238,250.
- [Official EVT commit](https://github.com/openwch/ch32x035/tree/c6253942b29ff2a3f5e2829182a874afffa45881), independently checked locally.
- [IAP source](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.c#L64-L123) and [header](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.h#L19-L35).
- `EVT/EXAM/FLASH/BootAsUser/BOOT区域作为用户区使用说明.pdf`, SHA-256 `74a61aea48c57833a01cbb837339ccd39aa93ceab3df793ba7b39a16c86eb3a0`, at the same EVT pin.

**WCH-Link distribution search**

- [Official metadata](https://www.wch.cn/api/official/website/common/relationFiles?fileName=WCH-LinkUtility_ZIP.html), file ID 418 on the Chinese site. IDs are site-specific. English `/download/file?id=418` returned a PDF, not this ZIP.
- Official retrieval attempted `https://api1.wch.cn/api/official/website/common/downloadFile?fileName=WCH-LinkUtility.ZIP`, as well as the `www.wch.cn` API endpoint. Both returned the refresh error recorded above.
- [WeAct mirror at fixed commit](https://github.com/WeActStudio/WeActStudio.WchLinkW/blob/d02cf4fd01047746e1a2e27f3490d43d4038a2a4/WCH-LinkUtility%20.ZIP).
- Mirror ZIP SHA-256: `e0c09421a7f38df5015ca52c2e94a10a9c50b5bf4eca58c052c7c9b1583850ac`. Local list: `tmp/r3-isp-followup/link-mirror-members.txt`. Its probe firmware is not the X035 factory loader.

The keyboard region labels and 18,648-byte stock-image length are supplied task constraints. This follow-up does not independently identify the keyboard or authenticate its stock image.
