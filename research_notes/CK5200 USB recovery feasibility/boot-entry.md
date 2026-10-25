# CH32X035 PC17 power-up entry and CK5200 recovery limits

## 1. Does PC17/USBD+ high at power-up select factory ISP?

### Takeaway

**The PC17-high power-up rule is confirmed by WCH. Its use through a CK5200 USB-C connector remains unverified.** The rule selects the BOOT area. It does not prove that this keyboard still contains WCH's factory loader, that a connector reaches PC17, or that a restore would preserve Clicks data.

### Cited Findings

#### Source versions and provenance

Research date: 2026-10-16. PDFs were downloaded and parsed offline. Physical PDF pages below are one-based. Printed page numbers are stated separately because the English RM's offset changes within the document.

| Source | Version; physical pages | SHA-256; provenance |
| --- | --- | --- |
| [WCH English datasheet, direct download](https://www.wch-ic.com/download/file?id=381) | V2.3; 47 | `1714c07d9448bc87ec667a602eba3e4c784a6ca7ca25ef125445df0ee4cc755d` |
| [WCH English RM, direct download](https://www.wch-ic.com/download/file?id=382) | V1.9; 261 | `c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a` |
| [WCH Chinese datasheet, direct download](https://file.wch.cn/download/file?id=443) | V2.3; 41 | `a3f2a0f66cfc75368ed5d71b44cfba0d18170efe1a3295bc90a8b647a4913aba` |
| [WCH Chinese RM, direct download](https://file.wch.cn/download/file?id=445) | V1.9; 246 | `c7e301eac4790ca1ba112f946bf057139ec7f36be28e142cafc1c286bcc9daa4` |
| [WCH evaluation-board reference, pinned official SDK](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf) | V1.3; 12 | `0b4d8179e08aec85a9582b5728fc6c0129b754e7c24a27b604f3ce049a33c5b8` |
| [WCH BOOT-as-user guide, pinned official SDK](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/BOOT%E5%8C%BA%E5%9F%9F%E4%BD%9C%E4%B8%BA%E7%94%A8%E6%88%B7%E5%8C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.pdf) | V1.2; 4 | `74a61aea48c57833a01cbb837339ccd39aa93ceab3df793ba7b39a16c86eb3a0` |

- The direct English downloads are byte-identical to the [datasheet](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/74c247d6d518052316b35b68ddc82db2a70afc52/datasheet_en/CH32X035DS0.PDF) and [RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/74c247d6d518052316b35b68ddc82db2a70afc52/datasheet_en/CH32X035RM.PDF) in mirror commit `74c247d6d518052316b35b68ddc82db2a70afc52`. This strengthens the prior RM provenance: the inspected bytes now also came directly from WCH. The [WCH product page](https://www.wch-ic.com/products/CH32X035.html) links the corresponding download landing pages. These are the current responses from those download IDs, not a claim about unpublished revisions.
- The local SDK at `keyboards/ck5200/external/ch32x035` is commit `c6253942b29ff2a3f5e2829182a874afffa45881`, matching [pins.env](../../keyboards/ck5200/scripts/pins.env#L7). All EVT code links in this note use that exact commit.

#### Entry and reset rules

| Question | Documented answer | Exact source |
| --- | --- | --- |
| Is PC17 a BOOT input? | Yes. "PC17 is the BOOT detection pin. Upon power-up, PC17 is high, causing the chip to enter the BOOT area." | [DS V2.3](https://www.wch-ic.com/download/file?id=381), chapter 2 pin notes, note 8, physical p.21 / printed p.19. |
| Is it USB D+? | EVT identifies USB pins as PC16 (DM), PC17 (DP). | [EVT V1.3](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf), section 5, physical/printed p.12. |
| What sequence does WCH document? | Hold the Download button while connecting USB or serial to the PC; the chip powers up and detects PC17 high; release the button, then use the ISP tool. This is source evidence, not an authorized CK5200 procedure. | Same EVT section 5, p.12. |
| Does an ordinary reset count as power-up? | RM distinguishes power reset, POR/PDR, from system reset, including NRST, watchdogs and software reset. It does not specify PC17 resampling for all those system resets. | [RM V1.9](https://www.wch-ic.com/download/file?id=382), sections 3.2.1–3.2.2, physical p.13 / printed p.11. |
| Can software select BOOT? | `FLASH_STATR.BOOT_MODE`, bit 14, selects BOOT after software reset when 1, user area when 0, in combination with `BOOT_AVA`. | RM section 20.3.4, physical p.240 / printed p.239; [Chinese RM](https://file.wch.cn/download/file?id=445), physical/printed p.227. |
| Is a USB bus reset a power reset? | USB bus reset sets a USB interrupt flag for firmware to handle. It is not the documented POR/PDR entry event. | RM section 18.2.2, physical p.221 / printed p.220; compare section 3.2 above. |

- `SystemReset_StartMode()` first unlocks flash, writes `FLASH_KEY1` then `FLASH_KEY2` to `BOOT_MODEKEYR`, clears bit 14, sets it for `Start_Mode_BOOT`, then locks flash. **This function does not itself reset the MCU.** See [WCH driver lines 742–764](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/SRC/Peripheral/src/ch32x035_flash.c#L742-L764).
- The register table names `R32_BOOT_MODEKEYR` at `0x40022028`; section 20.3.10 prints `BOOT_MODEKEYP`. The unlock sequence is `0x45670123`, then `0xCDEF89AB`. Preserve this spelling difference as a document inconsistency, not a second register. [RM](https://www.wch-ic.com/download/file?id=382), physical p.239 / printed p.238 and physical p.244 / printed p.243.
- `BOOT_LOCK` bit 15 is a lock flag; `BOOT_MODE` bit 14 is the software-reset selector; `BOOT_STATUS` bit 13 reports the current program source; `BOOT_AVA` bit 12 is read-only initial-configuration status, with 1 meaning BOOT startup. The manual does not provide a complete truth table for the interaction of `BOOT_AVA`, PC17 and all reset sources. [RM](https://www.wch-ic.com/download/file?id=382), section 20.3.4, physical p.240 / printed p.239.
- The documented user-option byte fields are RDPR, USER (`RST_MODE`, `STANDYRST`, `STOPRST`, `IWDGSW` and reserved bits), Data0/1 and WRPR0–3. **There is no documented user-option `BOOT0`, `nBOOT0` or ISP-disable bit in that table.** `RST_MODE` controls the external reset pin, not PC17 entry. The EVT board guide's older "BOOT0/1" wording on p.2 must not be converted into an STM32-style option-byte model. [RM](https://www.wch-ic.com/download/file?id=382), section 20.5, physical pp.248–249 / printed pp.247–248; [EVT](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf), pp.2, 12.

#### D+ pull-up and timing

- WCH's normal USB device example enables PC17 input pull-up in software, then selects a USB pull-up and enables the USB controller. That happens during application initialization. A normal D+ high level after initialization is therefore not evidence of the earlier BOOT-entry condition. [EVT USB code lines 121–162](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/USB/USBFS/DEVICE/CH372Device/User/ch32x035_usbfs_device.c#L121-L162).
- The USB pull-up is software-controlled. `UDP_PUE` selects its mode, and GPIO configuration controls enable. The documented modes include 10 kΩ for the stated 5 V use and 1.5 kΩ for the stated 3.3 V use. These are peripheral settings, not a specified external recovery-adapter circuit. [RM](https://www.wch-ic.com/download/file?id=382), section 8.3.2.4, physical pp.75–77 / printed pp.74–76; section 18.2.2, physical p.221 / printed p.220.
- The datasheet gives POR/PDR typical thresholds of 1.8/1.78 V in table 3-4, and POR duration of 4/17/24 ms minimum/typical/maximum. It also gives general GPIO input thresholds in table 3-13. **Neither table defines PC17 BOOT sample time, setup/hold time, or a dedicated boot-sampling threshold.** [DS](https://www.wch-ic.com/download/file?id=381), physical p.26 / printed p.24 and physical p.29 / printed p.27.

### Inferences

- **[INFERENCE] The relevant event is a real MCU power-up.** Interrupting one connector's VBUS is insufficient if another path holds MCU VDD up. USB detach, a host USB reset, and `NVIC_SystemReset()` must not be treated as equivalent to the documented PC17-high power-up sequence. This follows from the separate power/system/USB-reset descriptions above. Connector routing and alternate power paths belong to the PCB/interposer study.
- **[INFERENCE] There is no conflict between a working normal USB keyboard and PC17 boot selection.** PC17 can be low or undriven during startup, then become high when user firmware enables its USB pull-up. An external circuit that forces it high earlier changes that sequence. The evidence does not prove that an added permanent pull-up would preserve normal enumeration.
- **[INFERENCE] Software BOOT selection is not independent recovery from arbitrary dead firmware.** Code must execute the selector and reset path. The documented external power-up condition is the relevant candidate for bypassing broken main-flash code.

### Gaps

- No WCH guarantee was found for PC17 boot sampling after NRST, watchdog reset, or an ordinary software reset without selector setup. Power-up-only is the documented external method; it is not a proven universal silicon prohibition on every other reset route.
- No exact PC17 sample window, required high duration, dedicated threshold, release deadline, USB/UART arbitration interval, or factory automatic-detection timeout was found. The POR timing table cannot supply these values.
- No complete initial-configuration/`BOOT_AVA` programming model was found. There is no basis here to assert either that Clicks changed a persistent selector or that no manufacturing configuration can affect startup.
- The CK5200's connector-to-PC17 path, VDD discharge, backfeed, and loading are not established by these chip documents. No resistor value, delay, or passive adapter is validated by this note.

## 2. Is factory BOOT immutable, and can it survive failed Clicks code?

### Takeaway

**Factory BOOT is separate from the presumed Clicks installer, but it is not immutable ROM.** WCH explicitly supports replacing it through a debug programmer. Read protection can still allow ISP identification while blocking the useful flash operation.

### Cited Findings

| Region | Meaning and evidence |
| --- | --- |
| `0x08000000–0x0800F7FF` | 62 KiB Code FLASH, 256-byte pages. [RM](https://www.wch-ic.com/download/file?id=382), section 20.1, physical p.238 / printed p.237. |
| `0x1FFF0000–0x1FFF0CFF` | 3,328-byte System FLASH / BOOT / launcher region. Same source. |
| `0x00000000` | Boot-dependent alias of Code FLASH or system memory in the memory map. This alias is not proof that physical System FLASH equals user flash at `0x08000000`. [RM](https://www.wch-ic.com/download/file?id=382), section 1.2, physical p.5 / printed p.3. |
| CK5200 `0x0000–0x1FFF` | Presumed 8 KiB Clicks installer in Code FLASH. Its bytes and independent entry behavior are still missing. The stock download begins at linked `0x2000`, not at zero. [Existing artifact audit](../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#rechecked-artifacts). |

- WCH describes the system loader as "Factory-cured" but immediately states that the area is available for user use through WCH-LinkUtility. The immutable area described nearby is the separate vendor-configuration region, not the BOOT code. [RM](https://www.wch-ic.com/download/file?id=382), section 1.2.1, physical p.6 / printed p.4; [DS](https://www.wch-ic.com/download/file?id=381), section 1.4.3, physical p.6 / printed p.4.
- The official BootAsUser example explicitly uses the 3,328-byte BOOT area for user code. It says user code cannot erase that area. [main.c lines 13–26](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/User/main.c#L13-L26).
- The accompanying Chinese guide states that existing BOOT code is overwritten and boot-jump functions then become unavailable; this function uses Link tooling and a HEX file. It identifies CH32X035 as supported. These are direct translations of sections I and III, especially III.3–5. [BOOT-as-user guide V1.2](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/BOOT%E5%8C%BA%E5%9F%9F%E4%BD%9C%E4%B8%BA%E7%94%A8%E6%88%B7%E5%8C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.pdf), physical/printed pp.1, 3–4.
- Version detail matters: `main.c` says LinkUtility V2.40 or later, but the included V1.2 guide says greater than 2.60. The guide also exempts X035/V006 from the earlier 1 µs cross-region jump-delay warning. These are discrepancies in the pinned bundle, not a verified loader revision history. The replacement capability is clear in both sources. [Code](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/User/main.c#L13-L26); [guide](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/BOOT%E5%8C%BA%E5%9F%9F%E4%BD%9C%E4%B8%BA%E7%94%A8%E6%88%B7%E5%8C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.pdf), p.3.
- The example linker assigns `BFLASH` to `0x1fff0000`, length 3328, separately from `FLASH` at zero, length 62K. Its `.bxx` startup performs a software reset. This is additional source evidence that reset routing and the address-zero alias must be distinguished from the physical storage region. [Link.ld](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/Ld/Link.ld); [startup lines 13–23](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/Startup/startup_ch32x035.S#L13-L23).
- RDPR=`0xA5` with its valid complement means unprotected. Other RDPR values mean read-protected. In that state, main-memory pages 0–7, totaling 2 KiB, are automatically write-protected. From system boot code, SDI/debug, or RAM, main memory cannot be erased/programmed except by whole-chip erase. User-option writes remain possible; releasing read protection automatically erases the user area. [RM](https://www.wch-ic.com/download/file?id=382), section 20.2.2, physical p.238 / printed p.237; section 20.5, physical p.248 / printed p.247.
- WRPR3 bit 7 is described as protection for sector 62, the 3,328-byte system memory. This is separate from runtime `BOOT_LOCK` and does not establish the CK5200's setting. [RM](https://www.wch-ic.com/download/file?id=382), section 20.5, physical p.249 / printed p.248.
- The WCH IAP sample is a different main-flash design, with application base `0x08005000` and commands `0x80–0x84`. It is not evidence for the CK5200 installer at `0x0000–0x1FFF` or the WCH factory loader. [Prior source comparison](../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#public-test-app-history-and-wch-example).

### Inferences

- **[INFERENCE] If the original factory loader is intact and the documented PC17 power-up selection can be reached, main-flash application failure should not be needed for entry.** The separate BOOT storage also makes entry plausible when the presumed Clicks installer is corrupt. This is a chip-level conditional conclusion, not a demonstrated CK5200 restore.
- **[INFERENCE] Clicks could have replaced System FLASH during manufacturing using supported WCH tooling.** The primary documents prove that capability; they do not show that Clicks used it. Therefore, "factory ROM must still be there" is refuted, while "Clicks removed it" is unverified.
- **[INFERENCE] A normal application flash error is not documented as able to erase System FLASH directly.** The EVT explicitly disallows user-code BOOT erasure. This bounded protection does not establish immunity to debug-tool overwrite, boot configuration changes, or every possible fault.
- **[INFERENCE] Read protection is a programming/preservation gate, not proof that entry is disabled.** The independent logs in section 3 show protected devices answering ISP identification. Clearing protection is not a harmless recovery step, because the RM specifies erasure.
- **[INFERENCE] Recovering only the downloaded application cannot recreate missing Clicks installer bytes or lost identity/configuration data.** The downloaded artifacts omit the zero-based installer region. Entry and restoration completeness must remain separate acceptance checks. [Artifact audit](../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#rechecked-artifacts).

### Gaps

- The CK5200 System FLASH contents, factory-loader version, `BOOT_AVA`, RDPR, WRPR and actual initial boot path remain unread. I cannot verify them.
- No primary per-version factory-loader changelog, exact factory-loader source, or complete PC17 selector truth table was found. The current RM and DS have no identified boot-entry revision table. The EVT BootAsUser evidence is stronger than assuming immutability from older wording.
- Full preservation, selective erase boundaries, and host-tool side effects belong to the separate ISP/erase study. Nothing here authorizes or validates a protection change, flash command, configuration reset, or restoration image.

## 3. What do demonstrated community logs prove, and what gates remain?

### Takeaway

**Public logs demonstrate CH32X035 factory-ISP communication, including on read-protected parts, but none demonstrate CK5200 recovery.** They also show that loader versions and USB IDs differ. Reports about a short window or NRST failure lack the controlled traces needed to set adapter timing.

### Cited Findings

| Public evidence | Exact observed variant/version | What it establishes |
| --- | --- | --- |
| [wchisp issue #38](https://github.com/ch32-rs/wchisp/issues/38), 2023-08-05 | `CH32X035G8U6[0x5623]`; `BTVER ... 02.40`; USB `4348:55e0`; RDPR `0xA5` | Full request/reply log for identify and configuration. The title says "appears to work"; the posted log is not a complete restore demonstration. |
| [wchisp issue #43](https://github.com/ch32-rs/wchisp/issues/43), 2024-01-11 | `CH32X035G8U6[0x5623]`; `02.60`; USB `1a86:55e0`; RDPR `0xA5` | The same named package can report a different USB vendor ID and loader version. A tool that matches only the earlier ID can miss a responding loader. |
| [#43 protected-part log](https://github.com/ch32-rs/wchisp/issues/43#issuecomment-1904650242) | `CH32X035F8U6[0x5e23]`; `02.60`; USB `1a86:55e0`; RDPR `0xFF` | Protected firmware still permits ISP identify/config responses. A second log is reported after erasing a chip, but the two UIDs differ, so it is not proof of a same-device before/after transition. |
| [wchisp issue #68](https://github.com/ch32-rs/wchisp/issues/68), 2024-10-11 | WeAct `CH32X035F8U6[0x5e23]`; `02.60`; USB `1a86:55e0`; RDPR `0xFF` | Identification succeeds; the tool reports erase/write success but final verification returns `f500` and `Error: Verify failed, mismatch`. Entry and optimistic host messages do not establish successful programming. |
| [#68 WCH tool error report](https://github.com/ch32-rs/wchisp/issues/68#issuecomment-2558167356) | Same board family; exact WCHISPStudio version not supplied | Reports `Read-Protect is enabled,Please disable code-protect first!`. This is consistent with the logged protected state and RM restrictions. Advice in comments to reset protection/configuration is not evidence of preservation. |

- The `64KiB` capacity displayed by these historical tools conflicts with current WCH's 62 KiB main-memory map. Use the RM address bounds, not that host summary, for physical region reasoning. [Issue #38](https://github.com/ch32-rs/wchisp/issues/38); [RM section 20.1](https://www.wch-ic.com/download/file?id=382), physical p.238 / printed p.237.
- The supported alternate factory transport is USART2 on PA2 TX and PA3 RX; WCH's ISP procedure still calls for PC17 high at power-up. This is not a separate BOOT pin or an ordinary USB-to-UART cable solution. [EVT V1.3](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf), section 5, physical/printed p.12.

### Inferences

- **[INFERENCE] UART is not yet a demonstrated non-invasive CK5200 alternative.** It would require a documented exposed route to PA2/PA3 plus the same external entry condition. No such route is established here. PCB evidence belongs to the other study.
- **[INFERENCE] Critical gates for the PC17 hypothesis are:** (1) intact factory BOOT and compatible initial configuration; (2) proven connector path to PC17 and real VDD power-up; (3) controlled entry/release timing compatible with USB; (4) protection and restore behavior that preserve all required Clicks regions; and (5) a usable recovery host. The first three concern entry; successful entry cannot satisfy the last two.
- **[INFERENCE] The current classification is:** confirmed at chip-document level, plausible but unverified for application-independent CK5200 entry, and insufficient for R3. The existing [R3 decision](../../keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md) and [iPhone transport study](../../keyboards/ck5200/docs/iphone-recovery-transport.md) remain separate requirements. Raw factory ISP enumeration would not prove an iPhone app can use it or restore the keyboard intact.

### Gaps

- An [EEVblog report, reply #3](https://www.eevblog.com/forum/microcontrollers/bootloaders-and-cheap-risc-v-(ch32x035-and-ch32v003)/msg5865765/#msg5865765) says NRST did not enter CH32X035 BOOT but power-on did. It gives no exact package, loader version, or signal trace. Treat this only as a lead consistent with WCH's power-up wording, not controlled proof of reset behavior.
- [wchisp PR #114](https://github.com/ch32-rs/wchisp/pull/114) reports a roughly 3–5 second USB availability window on an unspecified CH32X035. It gives no loader-version log or timing trace. The [F8U6/02.60 report in #43](https://github.com/ch32-rs/wchisp/issues/43#issuecomment-1904650242) guesses an idle timeout; its logs do not measure one. No exact factory automatic-detection interval is established. In particular, the 2.5-second timeout elsewhere in the EEVblog thread concerns a custom CH32V003 loader, not CH32X035 factory ISP.
- No log found shows a CK5200 entering WCH ISP, entry after deliberately broken CK5200 code, or successful restoration with identity/authentication/configuration preserved. No hardware, enumeration, reset, ISP executable, or physical experiment was used in this research.
- No code tests were run because this is a document-only task. Verification comprised primary-PDF hashes, direct-source/mirror byte equality, PDF section/page checks, pinned EVT source reads and public request/reply logs. Downloaded evidence is under `tmp/r3-boot/`. `git check-ignore tmp/r3-boot/rm-official.pdf` returned no match, so that scratch path is currently untracked rather than ignored; no ignore configuration was changed.
