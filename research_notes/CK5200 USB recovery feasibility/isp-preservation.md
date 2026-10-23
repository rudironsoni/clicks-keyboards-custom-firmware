# CH32X035 factory ISP: app-only restoration and preservation

Follow-up, 2026-10-17: [actual WCH implementation analysis](isp-loader-followup.md) and [V122 tail analysis](v122-tail.md).
The result is **outcome 2, exact vendor dependency**, with a [ready-to-send, unsent inquiry](vendor-inquiry.md).
The original source assessment below remains bounded to the inspected interfaces and artifacts.

## 1. Can the existing factory-ISP clients preserve this keyboard?

### Takeaway

**No preservation-safe restoration path is established. The pinned `wchisp` client is unsuitable even if factory ISP entry becomes possible.** Its program and verify routines start at zero. Its erase command contains a count, not an address range. A modified program offset alone does not solve erase, read protection, unknown data, or interruption safety. [W-flash]; [W-proto]

This is a source-only assessment made on 2026-10-16. No keyboard access, enumeration, ISP executable or library execution, firmware command execution, reset, unlock, or physical test occurred. The approved repository point supplied by the coordinator is `d981763e6e96b143afd74ded9d11c72603e47039`, on `feat/r3-usb-c-recovery-study`. Downloads and source clones are under untracked `tmp/r3-isp/`, excluded from publication.

### Cited Findings

#### Source pins and exact official package

- `ch32-rs/wchisp`: inspected commit **`553327eeeea9fdc30414f1e5c74d1ff14b228a26`**, including actual `src/` and device YAML, not just the README. All W-* links below use this pin. [W-pin]
- Official WCH EVT: `openwch/ch32x035` commit **`c6253942b29ff2a3f5e2829182a874afffa45881`**. Its README states 62 KB Flash and 20 KB SRAM. [EVT-readme]
- Official reference manual: **CH32X035RM.PDF V1.9**, retrieved from `https://www.wch-ic.com/download/file?id=382`. SHA-256: `c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a`. Page references below are printed page numbers; PDF page numbers are one larger. [RM]
- Official datasheet: **CH32X035DS0.PDF V2.3**, retrieved from `https://www.wch-ic.com/download/file?id=381`. SHA-256: `1714c07d9448bc87ec667a602eba3e4c784a6ca7ca25ef125445df0ee4cc755d`. [DS]
- The official cross-platform ISP implementation package is **`WCHISPTool_CMD.ZIP`**, download `https://www.wch-ic.com/download/file?id=365`. SHA-256 of the retrieved archive: **`2da0b385bc789ab9cd468dd6dc29ec117a98dd00ad92e5ad5c9fb5b66868c4bd`**. Exact inspected members:
  - `WCHISPTool_CMD/Linux/src/IspCmdTool.cpp`, source SHA-256 `b33b072a7c598252a69a41058afdc420937817952f40c26ef6dac35fd84c50f4`.
  - `WCHISPTool_CMD/Linux/lib/x64/dynamic/WCH55XISPDLL.H`, header SHA-256 `b333152ff7a679e4bb170fddcf9fa107bf82750bf8cc55cada5615e6f6541ce6`.
  - `WCHISPTool_CMD/Linux/lib/x64/dynamic/libwch55xisp-4.0.0.so`, binary SHA-256 `b73d34967fd13e4f1f71749019666d826601e16c48ee02068196add964cc1bf8`.
  - `WCHISPTool_CMD/WCHISPTool_CMD Command Line Programming Tool Instruction.pdf`, especially §§1.4–1.5.
  The package supplies wrapper source, public headers, and compiled libraries. It does **not** supply the underlying ISP library implementation source in the inspected archive. No library was loaded or executed. This is the located official implementation, not an assumed `isp55e0lib` or `ispapi` repository. [Official-package]
- **Version contradiction:** the WCH page/API advertises package version **3.6**, upload date **2023-10-19**, and 3.19 MB. The retrieved ZIP is 4,143,391 bytes, has `IspCmdTool.cpp` line 34 declaring **`V3.70`**, and contains libraries named **`libwch55xisp-4.0.0`**. Treat the archive hash as the identifier. Do not assign all three version labels to the same component. [Official-metadata]; [Official-package]

#### Memory that must survive

The OEM region labels and stock-image identity below are **coordinator-supplied constraints**, not discoveries from WCH documents. Main-flash offsets in this note omit the `0x08000000` physical base. They must not be confused with similarly numbered System FLASH addresses.

| Region | Preservation requirement or verified geometry |
|---|---|
| Main flash offsets `[0x0000,0x2000)` | Unknown OEM installer, supplied requirement: never erase or program it. This is not the WCH System FLASH loader. |
| Stock application `[0x2000,0x68d8)` | Supplied 18,648-byte image, SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`. Inclusive last byte `0x68d7`. The image was not independently rehashed in this task. |
| Page span `[0x2000,0x6900)` | Calculated from the supplied image and documented 256-byte pages: 73 pages. Last-page bytes `[0x68d8,0x6900)` are **40 extra bytes**, not automatically authorized padding. [RM, §20.1][RM] |
| Nominal full app region `[0x2000,0x8a00)` | Supplied boundary, 106 pages. It is not evidence that bytes beyond the stock image can be erased. |
| Main flash pages `[0xf600,0xf700)` and `[0xf700,0xf800)` | Supplied device-specific configuration/serial locations. Preserve both complete pages, plus all other unknown bytes. |
| Main flash physical `[0x08000000,0x0800f800)` | 62 KiB, 248 pages of 256 bytes. Page 247 is `0x0800f700..0x0800f7ff`. Write protection uses 2 KiB groups, eight pages each. [RM, §20.1, printed p.237][RM] |
| System FLASH loader `[0x1fff0000,0x1fff0d00)` | 3,328 bytes. WCH calls it launcher/BOOT code. This is a separate region, not the first 8 KiB of main flash. [RM, §20.1][RM] |
| Manufacturer configuration `[0x1ffff700,0x1ffff800)` | Factory locked, separate from main-flash offset `0xf700`. [RM, §20.1][RM] |
| User option bytes `[0x1ffff800,0x1ffff900)` | Separate 256-byte region. Holds protection and other configuration. Preserve it, do not apply a generic config reset. [RM, §20.5][RM] |
| Electronic signature | Read-only 96-bit UID at `0x1ffff7e8`, `0x1ffff7ec`, `0x1ffff7f0`; flash-capacity register at `0x1ffff7e0`. [RM, ch.19, printed pp.235–236][RM] |
| External authentication chip/state | Supplied preservation concern. Its protocol and state are outside the inspected MCU ISP sources. No preservation claim about it follows from a successful MCU flash compare. |

- WCH describes System FLASH as reusable through WCH-LinkUtility. Thus, calling the factory loader “immutable ROM” would be wrong for this part. Separate location does not establish that every programming or unlock operation preserves it. [DS, §1.4.3][DS]
- **Read-protection removal is disallowed for a concrete reason:** RM §20.2.2 states that protected main memory cannot be erased/programmed from system boot code, SDI/SWD, or RAM except by entire-chip erase. Releasing protection automatically erases the entire user area. §20.5.4 explicitly says writing the unprotect value `0xa5` first causes entire flash erasure. It is not a non-destructive prerequisite. [RM, printed pp.237,249][RM]

#### Proof that current `wchisp` is unsuitable

1. **Programming starts at zero:** `src/flashing.rs:208–228` sets `let mut address = 0x0`, sends 56-byte chunks, advances by chunk length, and sends an empty final PROGRAM packet. It accepts no app base. `verify()` independently starts at zero at lines 262–278. [W-flash]
2. **ELF/HEX addresses do not rescue it:** `src/format.rs:173–188` subtracts the lowest section address when constructing a flat byte vector. `flash()` then programs that vector from zero. An ELF linked at `0x2000` or `0x08002000` does not retain that destination through this path. [W-format]
3. **Erase has no start-address field:** `src/protocol.rs:168–175` emits `a4 04 00` followed by one little-endian `u32` named `sectors`. `erase_code()` has only a sector-count argument. There is no encoded skip over the OEM installer. [W-proto]; [W-erase]
4. **Minimum eight is enforced by this host:** `src/device.rs:222–229` returns eight 1-KiB sectors for type `0x23`; `src/flashing.rs:456–465` raises smaller requests to that minimum. This proves the client's behavior. It does **not** prove that every X035 loader independently enforces the same minimum. [W-device]; [W-erase]
5. **This image would be padded and oversized again:** `src/main.rs:237–246,375–379` zero-pads to a 1-KiB boundary, then requests `padded_length/1024 + 1` sectors. For 18,648 bytes this yields **19,456 bytes (`0x4c00`) programmed at zero and 20 sectors requested**, not the 73 app pages at `0x2000`. Under the prefix-erase interpretation, the requested erase covers `[0,0x5000)`. The prefix interpretation is identified below as an inference, not a recovered loader implementation. [W-main]
6. **Skipping erase is insufficient:** the no-erase flag bypasses A4 but leaves programming at zero. Prepending zeroes or `0xff` is not a preservation operation, and restoring guessed installer bytes later would violate the “untouched” requirement. The client has no sparse skip representation in `flash()`. [W-main]; [W-flash]; [W-format]
7. **The client's protection status is incomplete for X035:** `support_code_flash_protect()` lists types `0x14,0x15,0x17,0x18,0x19,0x20`, not `0x23`. Therefore `code_flash_protected` in initialization is false for X035 regardless of RDPR. The YAML still decodes RDPR as protected unless `0xa5`. Absence of a printed protection warning cannot establish that this chip is unprotected. [W-device, lines 240–243][W-device]; [W-init, lines 44–63][W-init]; [W-db, lines 12–22][W-db]
8. **Do not infer auto-unlock from comments:** the comment “unprotect -> erase -> flash -> verify -> reset” is not the actual CLI flow. The inspected `Flash` arm does not call `unprotect()`. Explicit configuration commands can write A8. `unprotect()` changes RDPR/nRDPR and WPR then resets. This is separate dangerous behavior, not needed to prove the normal flash path unsuitable. [W-main, lines 227–269,339–366][W-main]; [W-init, lines 172–194][W-init]

#### Actual X035 evidence and contradictions

- Issue **#68**, opened 2024-10-11, contains a concrete CH32X035F8U6 trace: `1a86:55e0`, identify response `a1 00 02 00 5e 23`, loader bytes `00 02 06 00` displayed as **02.60**, RDPR `ff`, A4 request `a4 04 00 08 00 00 00`, A4/A5 responses `... 00 00`, then A6 response **`a6 00 02 00 f5 00`** and `Error: Verify failed, mismatch`. This is evidence of framing and failure, not proof that the earlier erase/program physically succeeded or preserved anything. [X035-log]
- In that same issue a user reports official WCHISPStudio saying **`Read-Protect is enabled,Please disable code-protect first!`**. A later comment contrasts a failing WeAct board with RDPR `0xff` and a working EVT board with RDPR `0xa5`. Suggestions to reset configuration or use WCH-Link are destructive workarounds for development boards, not acceptable restoration steps for this keyboard. [X035-protect]; [X035-compare]
- The old log reports **64 KiB**. The pinned current F8U6 database reports **62 KiB**, matching RM main-memory geometry. PR #75 separately reports X035 working and corrects an old “4k” sector description to “1k”. Neither success report proves app-only erasure. [X035-log]; [W-db]; [X035-tested]; [RM]
- Another current-source discrepancy is F7P6: pinned `wchisp` lists **49,152 bytes**, while datasheet V2.3 Table 1-1 lists **62K**. Do not apply a family-wide capacity assumption to an unidentified package. [W-db, lines 87–89][W-db]; [DS, Table 1-1][DS]

### Inferences

- **[INFERENCE]** The A4 count-only operation is a prefix erase starting at main-flash offset zero in the conventional client model. Its use before zero-based programming and the lack of a start argument support that model. **The inspected host source does not prove “the loader always starts at zero” for every state, version, undocumented payload, or opcode.** Treat it as potentially destructive to the prefix, never as a safe range operation. [W-proto]; [W-main]; [W-flash]
- **[INFERENCE]** Even without resolving A4's exact physical footprint, current `wchisp` is disqualified: its first A5 targets zero, inside the required untouched installer. If protection rejects the operation, restoration fails; if it accepts the operation, installer preservation is at risk. [W-flash]; [RM]

### Gaps

- Actual keyboard ISP identity, loader version, RDPR/WPR state, and factory-loader integrity were not measured. They remain unknown.
- No X035 loader source or vendor wire specification was recovered that proves A4's internal start, count limits, alignment, end clamping, or behavior when a protected page is encountered.
- The OEM labels, stock hash, nominal app end, and external authentication state were supplied by the coordinator. This task does not independently prove their completeness. Unknown bytes remain protected by policy.

## 2. What factory-ISP protocol can an offline lab model honestly?

### Takeaway

The source supports a **version-tagged packet model**, not a safe hardware programmer. Keep factory USB ISP separate from the keyboard application's A1 dialect and from WCH's optional IAP examples. Verification is a device-side comparison against supplied bytes, **not a main-flash reader**. [W-proto]; [W-flash]; [EVT-iap-h]

### Cited Findings

#### Identity, transport, and version

- `src/transport/usb.rs:9–10,42–43,110–135,166–188`: the client accepts vendor IDs **`0x4348` or `0x1a86`**, product ID **`0x55e0`**; configuration 1, interface 0; bulk OUT **`0x02`**, bulk IN **`0x82`**; 64-byte receive buffer. It checks endpoint addresses but does not establish a unique keyboard identity. Both accepted VIDs are client policy; the cited X035 trace specifically observed `1a86:55e0`. [W-usb]; [X035-log]
- `devices/0x23-CH32X03x.yaml`: device type **`0x23`**, MCU type **`0x13`**. Chip IDs, converting the YAML decimal values to hex: **R8T6 `0x50`, C8T6 `0x51`, F8U6 `0x5e`, G8U6 `0x56`, G8R6 `0x5b`, F7P6 `0x57`**. X033F8P6 is `0x5a`, a different part. Identify payload order is **chip ID, device type**, not a single little-endian chip number. This table does not identify the keyboard's package. [W-db]; [W-init]
- A7 all-config reply parsing: payload `[0:2]` is skipped as the mask/header; configuration occupies `[2:14]`; loader bytes `[14:18]`; remaining bytes form the ISP UID. X035 uses the first eight UID bytes for XOR-key calculation. The client checks that the first three little-endian UID words sum modulo 65536 to the fourth. This **eight-byte ISP UID field is not a documented byte-for-byte copy of the RM's 96-bit ESIG UID**. Preserve the distinction. [W-init, lines 44–72][W-init]; [W-uid, lines 534–565][W-uid]; [RM, ch.19][RM]
- Only **X035 loader 02.60** is grounded here in a full inspected community trace. No tested compatibility range is established for this keyboard. The official API has `IsPreBTV230`, but that is a multi-family API flag, not evidence that X035 versions before 2.30 exist or support this workflow. [X035-log]; [Official-package, `WCH55XISPDLL.H:93–106`][Official-package]

#### Request contract

Common request form in the pinned source: **opcode `u8`, payload length `u16 LE`, payload**. USB transfers are bulk payloads, not HID reports or USB setup `bRequest` values. The following are observed source encodings, not instructions to transmit to the keyboard. [W-proto]; [W-usb]

| Opcode | Payload / source behavior | Side effect classification for the offline lab |
|---|---|---|
| **A1 Identify** | Length `0x12`; `chip_id:u8`, `device_type:u8`, ASCII `MCU ISP & WCH.CN`. Initial IDs `00 00`; response payload begins actual two IDs. Source initializes with two identify transactions. | Identification/session interaction. No flash write in this host operation. Device-wide absence of side effects is not proven by host source. [W-proto:147–157][W-proto]; [W-init:26–45][W-init] |
| **A2 ISP_END** | Length 1, `reason:u8`. Enum comment: 0 normal, 1 config set. `reset()` sends reason 1. | Ends session/reboots. Deny, including cleanup/reset-on-exit. [W-proto:18–23,159][W-proto]; [W-init:197–203][W-init] |
| **A3 ISP_KEY** | Client sends 30 zero seed bytes. Key derivation below. Reply payload byte 0 is checked against key-byte sum. | Changes protocol/session key state. Not an external authentication operation. Model in memory only. [W-flash:208–216][W-flash] |
| **A4 ERASE** | Length 4, `sectors:u32 LE`. No start address. X035 host minimum 8, host sector unit 1024 bytes. | Destructive. No admissible app-only erase encoding is established. Never reinterpret the u32 as an address or pack a page number into unused-looking bytes. [W-proto:168–175][W-proto]; [W-device:222–229][W-device] |
| **A5 PROGRAM** | `address:u32 LE`, one random padding byte, XORed data; length `5+n`; client data chunks up to 56 bytes. Final empty PROGRAM uses length 5 at end address. | Writes code flash and can finalize buffered data. First client address is 0. Address field existence alone does not prove legal arbitrary destinations, SRAM access, or page-local effects. [W-proto:176–190][W-proto]; [W-flash:208–228,423–431][W-flash] |
| **A6 VERIFY** | Same fields as A5; host supplies expected XORed bytes. Client accepts payload byte 0 equal to 0. | Comparison, not byte readback. Effective range/alignment and behavior under protection remain version-specific. No preservation proof for unknown bytes. [W-proto:191–204][W-proto]; [W-flash:262–278,446–453][W-flash] |
| **A7 READ_CONFIG** | Length 2; `mask:u8`, `00`. Client uses mask **`1f`** for all config. Masks defined: `07` writable-config group, `08` loader, `10` UID. Source explicitly says CH32X03x does not support bit-mask reads in the usual way. | Config/version/UID retrieval only. Do not treat it as arbitrary memory read. Use all-config X035 fixture, not a speculative mask-07 layout. [W-proto:205–208][W-proto]; [W-const][W-const]; [W-erase:490–497][W-erase] |
| **A8 WRITE_CONFIG** | `mask:u8`, zero byte, configuration data. | Persistent options/protection changes; may cause mass erase. Deny all variants, including “restore defaults,” “enable debug,” or writing back a supposedly unchanged template. [W-proto:209–216][W-proto]; [W-init:172–194][W-init]; [RM, §20.5.4][RM] |
| **A9 DATA_ERASE** | Length 5; four zero bytes, one count byte. Source includes `FIXME: is this correct?`. | Data-memory erase on supporting families. Deny for X035. This is not evidence for an addressable main-flash erase. [W-proto:245–262][W-proto] |
| **AA DATA_PROGRAM** | Address, padding, XORed data like A5. | EEPROM/data-flash programming on supporting families. Deny for X035. [W-proto:226–243][W-proto] |
| **AB DATA_READ** | Length 6; `address:u32 LE`, `len:u16 LE`. EEPROM path requests at most `0x3a` bytes. | Defined as data-flash read. X035 database has no EEPROM size; `dump_eeprom()` rejects zero-sized EEPROM. It is not a proven route to code flash or main-flash `0xf600/0xf700`. [W-proto:217–224][W-proto]; [W-flash:380–420][W-flash]; [W-db][W-db] |
| **C3 / C4** | OTP write/read constants and enum entries, but encoding falls through to `unimplemented!()` in this source. | Unsupported, deny. No X035 OTP contract follows. [W-const][W-const]; [W-proto:277–279][W-proto] |
| **C5 SET_BAUD** | Length 4, baudrate `u32 LE`. | Serial transport state, not needed for USB. Deny in a USB-only model. [W-proto:264–275][W-proto] |

- **XOR fixture:** let `s = sum(first_8_ISP_UID_bytes) mod 256`. For the all-zero seed, key bytes 0–6 are `s`, key byte 7 is `(s+chip_id) mod 256`. Each chunk XORs data byte `i` with `key[i mod 8]`; expected key checksum is the sum of all eight key bytes modulo 256. This is reversible transport encoding, not image signing or proof of OEM authenticity. [W-uid]; [W-flash]
- **Address distinction:** the host emits zero-based code offsets for A5/A6. It does not send physical addresses such as `0x08002000`. The serializer can encode any u32, but the target's admissible range is not validated by that fact. `0x2000` as an app offset is an unvalidated extension to the existing flow, not an observed keyboard command. [W-flash]; [W-proto]
- **Response framing has a serious host limitation:** `src/protocol.rs:316–327` reads length at raw offset 2 and payload at offset 4, but `if true` returns `Response::Ok` for any correctly sized reply. It ignores raw status/header and does not correlate the opcode. `flash_chunk()` and `erase_code()` only test `is_ok()`. A3 checks its checksum and A6 checks only payload byte 0. The offline parser must not inherit “well-framed means operation succeeded.” PR #110 reports this same issue; its proposed universal status-byte interpretation is not a substitute for an X035 loader specification. [W-proto]; [W-flash]; [W-erase]; [Status-pr]
- Exact X035 failure fixture: `a6 00 02 00 f5 00`. Preserve `f5 00` as a two-byte raw failure result. The evidence supports “comparison did not succeed,” not a complete semantic dictionary for `0xf5` or the unused/high status byte. [X035-log]

#### Protocols that must stay separate

- A byte value **A1** alone does not identify a protocol. In factory ISP it means the identify packet above. The keyboard application's A1 dialect is a coordinator-supplied separate interface; no WCH source here proves a bridge from that interface to factory ISP. Do not dispatch factory packets on the basis of application opcode similarity. [W-proto]
- WCH's optional **EVT USB/UART IAP** is installed application code, not the factory loader: `iap.h:20–29` sets physical app base `0x08005000` and opcodes **80 program, 81 erase, 82 verify, 83 end, 84 jump**. `iap.c:64–123` performs page erase/write in program handling; its first verify can flush the last page. Its end handler erases the page containing `CalAddr`, which `iap.h:35` defines as `0x0800f800-4`, namely **main-flash page `0xf700`**. This official sample would violate the keyboard preservation rule if used unchanged. Do not copy its semantics into factory A4/A5/A6. [EVT-iap-h]; [EVT-iap-c]
- The official ISP API exposes `WCH55x_ReadDataFlash(StartAddr, ...)`, `WCH55x_FlashProgramB(...)`, and `WCH55x_FlashVerifyB(...)`, plus config and OTP operations. The inspected header exposes no main-flash dump, no addressable page-erase API, and no SRAM-load-and-execute API. `IAPStartAddr` and `IsEraseAllCFlash` are settings, not documentation that setting them can preserve an unknown prefix. [Official-package, `WCH55XISPDLL.H:109–173,292–371`][Official-package]
- The official wrapper calls `WCH55x_FlashProgramB` or `WCH55x_FlashVerifyB` with binary buffers. **Its “verify” branch is not intrinsically read-only:** `IspCmdTool.cpp:176–184` conditionally clears data flash before the program/verify branch at line 186. The setting `IsClearDataFlash` controls that action. This is one reason not to run even an official verification command as a speculative probe. [Official-package, `Linux/src/IspCmdTool.cpp:165–205`][Official-package]

### Inferences

- **[INFERENCE]** A custom host can construct A5/A6 packets containing `0x2000`, but successful nonzero programming is not enough. Safety requires proving erase range and all implicit flush/write effects independently. [W-proto]; [W-flash]
- **[INFERENCE]** A6 can confirm known expected bytes if its loader semantics and status handling are established. It cannot supply the unknown OEM installer or device-specific data needed for a backup. Issue #77 explicitly retracts the assumption that verify implies flash dumping. [W-flash]; [No-reader]

### Gaps

- No complete X035 factory ISP specification for status codes, sequence requirements, program alignment, final-buffer flushing, retries, out-of-range addresses, or read-protected verification was found in the inspected material.
- No target-side proof establishes whether arbitrary offsets work, whether A5 does implicit page erase, whether A6 can finalize earlier writes, or whether an empty A5 touches the page containing its address. The optional EVT IAP demonstrates why these behaviors cannot be assumed from command names.
- No software model here can infer successful restoration, untouched bytes, or a usable backup from a generic ACK.

## 3. What would a safe extension need, and what must the builder assume today?

### Takeaway

**Model factory-ISP restoration as blocked by default.** A hypothetical custom extension needs a proven, version-specific way to erase only approved app pages without touching protection or other state. No such factory-USB command, nor a no-flash-write SRAM-helper route, is established by the inspected sources. [W-proto]; [Official-package]; [RM]

### Cited Findings

- The **hardware** supports 256-byte page erasure and page programming: RM §§20.4.4–20.4.5 describe page operations, and official `FLASH_Program/User/main.c:24–25,58–98` demonstrates operations at physical `0x08003000` with size 256. This proves capability for code already executing with appropriate access. It does not expose that capability through factory USB ISP. [RM]; [EVT-flash]
- The factory client command enum and official ISP API inspected above have no RAM upload or branch-to-address command. Their A2/ISP_END operations end ISP/reset; they are not arbitrary-PC execution APIs. [W-proto]; [Official-package, header lines 332–371][Official-package]
- WCH documents **2-wire SDI** on SWDIO and SWCLK, separate from the MCU USB peripheral. Official OpenOCD configuration explicitly names `interface wch2pin`. A debugger using a RAM work area through that interface is not evidence that the keyboard's direct USB factory loader accepts a RAM helper. No debug probe or stub was used here. [DS, §1.4.22][DS]; [WCH-2pin]
- Even a hypothetical RAM helper would not bypass the read-protection rule: RM §20.2.2 explicitly includes execution from the RAM area among contexts that cannot erase/program protected main memory except through whole-chip erasure. [RM]

### Inferences

#### Minimal extension evidence required

These are **[INFERENCE] acceptance conditions**, not claims that the required mechanism exists:

1. Pin the keyboard's exact package/chip ID and factory-loader bytes. Obtain a vendor contract or independently reviewed target implementation for that revision. Current host support and the 02.60 development-board log are insufficient. [W-db]; [X035-log]
2. Prove an erase operation with an explicit start and length/page list, or prove an equivalent operation that has no prefix erase. Establish the exact footprint and minimum for that loader. A4's current four-byte count does not meet this requirement. Do not append guessed bytes to A4. [W-proto]
3. Prove A5/A6 offset interpretation, page boundaries, implicit erase/flush behavior, final short packet behavior, and empty final packet behavior. Prove the exact success and failure response rules. [W-proto]; [W-flash]
4. Prove that read/write protection permits the proposed app operations **without** A8, option-byte erase, protection release, or debug enabling. A protected or unknown state blocks restoration. WPR protection of the installer is not permission to attempt an erase across it; rejection/skip/partial behavior is unknown. [RM]; [W-device]
5. Account for every changed byte. For the supplied image, `[0x68d8,0x6900)` must remain unchanged throughout success and interruption. Reading and rewriting that range is insufficient. Vendor evidence that these bytes are disposable would require a separately approved policy change. Erasing the entire nominal region through `0x8a00` is a different and larger mutation than restoring the 18,648-byte file. [RM, §20.1][RM]
6. Prove persistence and interruption behavior: whether a reply follows durable completion, whether a failed command can partly change a page, whether a timeout means “not executed” or “executed without reply,” what remains bootable after each interruption, and whether the loader is reachable without a working app. No automatic retry/reset can substitute for this evidence. Existing code performs separate erase/program/verify/reset steps, not an atomic transaction. [W-main]; [W-flash]
7. Prove all non-app regions remain untouched across both success and failure, including System FLASH and options. External authentication state requires its own evidence. A later byte-for-byte rewrite of an erased installer would still fail the requested untouched guarantee. [RM]; [W-proto]

#### Exact offline Python lab assumptions

The following is a proposed **[INFERENCE] conservative model contract**, grounded in the source limitations above. It is suitable for an offline builder, not hardware authorization.

| Item | Required model assumption |
|---|---|
| Transport | No USB/HID/serial library, enumeration, process invocation of ISP tools, or device-backed transport. Input is stored/source-derived packets only. |
| Identity | `chip_id`, `device_type`, `loader_version`, RDPR, WPR, and capacity start **unknown**. `0x23` plus an allowed ID is a parser fixture, not a discovered keyboard identity. Loader `00 02 06 00` is a named community fixture only. |
| Capacity | A named 62-KiB X035 profile uses main offsets `[0,0xf800)`, pages of `0x100`, protection groups of `0x800`. Package or version conflicts block profile selection. |
| Memory | Store unknown bytes as unknown/opaque values, not erased `0xff` or zeroes. Keep main flash, System FLASH, options, ESIG, external authentication state, and session key as distinct stores. |
| Image | Require the supplied length 18,648 and SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` before calling a candidate “this stock image.” Target bytes are `[0x2000,0x68d8)`. |
| Preservation | Default protected set is **everything outside the explicitly approved mutation set**. At minimum pin `[0,0x2000)`, both `0xf600/0xf700` pages, System FLASH, options, ESIG, and external state. Do not assume the rest is empty. |
| Erase rounding | Calculation may show `[0x2000,0x6900)`, but the 40-byte tail stays unknown/protected until evidence permits a change. `[0x2000,0x8a00)` is not the default erase plan. |
| Factory A4 | Decode `u32` count only. No start field and no invented page-range variant. Mark preservation-safe execution **unsupported**. An optional regression fixture may model prefix erase `[0,count*1024)` with host minimum 8, explicitly tagged “host/conventional model, target footprint unverified.” It must fail the preservation check. |
| Current-client regression | For the supplied image, model zero-padding to `0x4c00`, A4 count 20, and A5/A6 from zero. Reject before any modeled mutation because the proposed range intersects the installer. |
| Custom A5 offset | Decode u32 offsets, but treat arbitrary-offset device acceptance and the physical effect range as unknown. A serializer accepting `0x2000` is not a restored-image result. |
| Status | Check framing, exact lengths, request/response opcode relation, and operation-specific fixtures. Unknown header/status/length means failure or unknown, never success. Keep operation status separate from transport success. Do not copy upstream `if true`. |
| Verify | A compare operation taking expected bytes. Never expose it as `read_flash()`, backup, or proof of preservation for unknown regions. Protected-state success remains unproven. |
| Forbidden operations | Reject A2, A4 execution, A5 execution, A8, A9, AA, C3, unknown opcodes, all unlock/config reset flows, and all RAM-execute claims. A1/A7/A3/A6 can be parsed/simulated from fixtures only, not sent. AB is not a supported X035 main-flash reader. |
| Errors/interruption | A disconnect, timeout, or malformed reply invalidates completion. Never assume rollback, page atomicity, durable completion, safe retry, or automatic recovery. If modeling a hypothetical mutation, distinguish possible partial effects from a guaranteed result; unsupported target behavior must remain unknown. |
| Success labels | Allow “packet decoded,” “image hash matched,” “range rejected,” or “hypothetical plan satisfies stated model assumptions.” Do not report “safe to flash,” “backup complete,” “OEM preserved,” or “restoration proven.” |

### Gaps

- **Guarantee today:** source inspection establishes that the current client does not implement the required preservation contract. It does not establish any safe keyboard mutation sequence.
- **Custom offset + erase pages:** packet serialization is feasible, but no proven factory-ISP page-selective erase primitive was found. This blocks an executable restoration plan. It is not proof that an undocumented extension is impossible.
- **RAM helper:** no evidenced factory-USB load/execute route was found without flash installation or protection changes. Official memory/SDI capability is not that route. Whether an undocumented one exists remains unknown; do not probe the only keyboard for it.
- **Read protection:** the keyboard's state is unknown. RM behavior and the 02.60 trace make protection release unacceptable. Whether factory A6 can safely compare known bytes while protected, without other side effects, is not established.
- **Interruption:** no vendor guarantee for A4/A5 atomicity, partial page effects, durable ACK, resume, retry idempotence, or preservation of all other memory under power loss was located. Factory-loader reachability after app damage is a separate unresolved entry question.
- Checks performed: pinned-source trace, official archive/header/source inspection, PDF text extraction, source-artifact hashes, and boundary arithmetic. Hardware checks, ISP execution, firmware writing, and restoration tests were intentionally excluded by the assignment. `pdftotext` was unavailable; PDF text was extracted with `pypdf` instead.

[W-pin]: https://github.com/ch32-rs/wchisp/commit/553327eeeea9fdc30414f1e5c74d1ff14b228a26
[W-proto]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs#L145-L329
[W-flash]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L206-L453
[W-erase]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L456-L497
[W-init]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L26-L203
[W-uid]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L534-L565
[W-device]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/device.rs#L216-L243
[W-db]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/devices/0x23-CH32X03x.yaml#L1-L92
[W-main]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/main.rs#L220-L379
[W-format]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/format.rs#L70-L189
[W-usb]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/transport/usb.rs#L9-L188
[W-const]: https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/constants.rs#L3-L38
[RM]: https://www.wch-ic.com/download/file?id=382
[DS]: https://www.wch-ic.com/download/file?id=381
[Official-package]: https://www.wch-ic.com/download/file?id=365
[Official-metadata]: https://www.wch-ic.com/api/official/website/common/relationFiles?fileName=WCHISPTool_CMD_ZIP.html
[EVT-readme]: https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/README.md#L10-L30
[EVT-flash]: https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/FLASH_Program/User/main.c#L24-L98
[EVT-iap-h]: https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.h#L19-L35
[EVT-iap-c]: https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.c#L18-L133
[X035-log]: https://github.com/ch32-rs/wchisp/issues/68
[X035-protect]: https://github.com/ch32-rs/wchisp/issues/68#issuecomment-2558167356
[X035-compare]: https://github.com/ch32-rs/wchisp/issues/68#issuecomment-3368736961
[X035-tested]: https://github.com/ch32-rs/wchisp/pull/75
[No-reader]: https://github.com/ch32-rs/wchisp/issues/77#issuecomment-2571372879
[Status-pr]: https://github.com/ch32-rs/wchisp/pull/110
[WCH-2pin]: https://github.com/openwch/openocd_wch/blob/e39efea3b937f4f195bc27b5c08ba75c6123f44a/scripts/interface/wch2pin.cfg#L1-L3
