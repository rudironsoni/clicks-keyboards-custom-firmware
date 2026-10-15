# CK-5200 recovery audit, 2026-10-14

## Result

**R3 remains UNMET. No demonstrated recovery route survives failure of the keyboard application to boot.**
The [2026-10-15 decision](adr/0001-independent-iphone-recovery.md) governs the next work and rejects patched-stock installation.
The missing evidence concerns the presumed Clicks installer at `0x0000–0x1FFF` and its independently reachable recovery protocol, if one exists.
This is not proof that the installer has no recovery mode. Its code is absent from the inspected artifacts.

The stock diagnostics baseline is complete. More getter reads or typing recordings cannot answer this boot-time question.
Current authorization remains in [flash-readiness.md](flash-readiness.md).

## Rechecked artifacts

Repository reference: `5f87e3295445fbd4b4f09a9217fd83016b20efc7`.
The following local files were rehashed during this audit:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `.stock/iKeyboard_CK-5200_V121_120.bin` | 18,572 | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |
| `.stock/iKeyboard_CK-5200_V122_120.bin` | 18,648 | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |
| `.stock/clicks-app-prod-release.apk` | Not used as an image | `fc60df6c69f0fb142892b9a8b531c60fb439b8d7e37bbde01886eea296499740` |
| `build/ck5200_qmk.bin`, host-test input only | 24,124 | `7799f866ee18ea4c3ba93852b5c6cb7b32864a29efc88fc79301af7f84d09623` |

Paths in this table are relative to `keyboards/ck5200/`. The custom image was not installed.

Both stock files have the first vector target `0x2000`. V122's reset jump targets `0x485C` when linked at `0x2000`.
Its application bytes span `0x2000–0x68D7`. They are not a dump starting at address zero.
The archived ELF's section at `0x2000` was compared byte-for-byte with V122 before fresh disassembly with LLVM's `+xwchc` support.
No installer image was found in `.stock/`. APK archive names and DEX distribution strings exposed no installer payload or installer URL.
This bounded inspection does not establish that no private vendor artifact exists.

## Why the current update paths cannot rescue a dead application

| Primary evidence | Observed behavior | Recovery consequence |
| --- | --- | --- |
| Stock V122, linked `0x2208–0x2218` and `0x3216` | The application callback invokes the command dispatcher. Update branches are inside this downloaded application. | The demonstrated command service depends on application execution. |
| Stock V122, `0x37A8–0x3808`, `0x380A–0x38DC` | A1 initializes transfer state. A2 fills pages based at `0x08008A00`, reserving four bytes for the marker. | These are staging operations, not a separately reachable boot service. |
| Stock V122, `0x38FE–0x390C`, `0x31B2–0x3214` | A3 passes four size bytes to the page read-modify-write routine at `0x08008A00`. | Acknowledgment establishes a staging action, not the installer's validation or rollback behavior. |
| [Custom USB dispatcher](../firmware/platform/ch32x035/protocol.c#L231), lines 231–274 | EP2 and EP3 both call `ck5200_update_handle` from TinyUSB application callbacks. EP3 also requires an active Apple session. | The Mac fallback bypasses the Apple session, not a failed application or USB stack. |
| [iOS session setup](../../../ios/ClicksInspector/Sources/AccessoryInspector.swift#L167), lines 167–193 | Requires a connected `EAAccessory`, advertised protocol, and two `EASession` streams. | The app has no separate installer connection path. |
| [iOS updater](../../../ios/ClicksInspector/Sources/CustomFirmwareController.swift#L419), lines 419–540 | Requires a link and known identity, then sends A1/A2/A3 and optionally A0. Default builds reject the update. | Installing a different app build would not create a missing keyboard-side recovery service. |
| [Platform hooks](../firmware/platform/ch32x035/platform.c#L81), lines 81–95 | `bootloader_jump()` and `mcu_reset()` both call `NVIC_SystemReset()`. | Neither implements entry into a verified rescue interface. |

The [custom linker](../firmware/platform/ch32x035/linker.ld#L1) bounds the image to `0x2000–0x89FF`.
The [staging writer](../firmware/platform/ch32x035/ck5200_staging.c#L90) restricts its own page writes to `0x08008A00–0x0800F4FF`.
These checks constrain those implementation paths. They do not prove what the unread installer copies or protects.

## What the restore test actually proves

Recompiled and ran the existing [restore harness](../tests/ck5200_restore_test.c) with the stock and custom inputs above. It passed.
It checks region bounds, complete staging transfers, retained simulated flash, and injected erase/program failures.

The harness calls the application update handler directly. Its `power_cycle()` resets software state but does not execute a CPU reset vector.
Its A0 check increments a counter. It neither executes the Clicks installer nor boots the staged image.
See harness lines 125–195 and [the mocked reboot branch](../firmware/platform/ch32x035/ck5200_staging.c#L256).
Therefore, its successful recovery cases assume the update handler remains callable after the modeled fault.
They cannot establish recovery when the application cannot execute.

## Current vendor and Apple sources

- The [official iPhone guide](https://app.clicks.tech/en/docs/iphone-get-started) describes firmware updates through the Clicks app.
  The [support page](https://www.clicks.tech/en/pages/support) provides no dead-application recovery procedure in the inspected text.
- The archived APK's DEX strings name `https://xinyi1.clicks.tech/firmwares-a.json`.
  A fresh read returned four Android entries: CA-1100 V112, CA-2100 V112, CA-2200 V112, and CA-2200 V115.
  It contains no CK-5200 or installer entry.
- A fresh request for `https://xinyi1.clicks.tech/CK-5200/iKeyboard_CK-5200_V122_120.bin` returned HTTP 403.
  The archived V122 hash remains verified. Current public download availability could not be verified.
  A 403 does not prove that the object is absent.
- [Apple transport research](iphone-recovery-transport.md) found no documented public raw-USB route for an ordinary iPhone app to the stated factory ISP.
  USBDriverKit's documented M-series iPad support does not establish support on the iPhone 15 Pro Max.

**[CORRECTION] "Recovery is Mac-only" was too broad.** Other host platforms may support raw USB.
No independent CK-5200 recovery procedure was demonstrated on any host in this audit.
Factory ISP access and an iPhone-reachable Clicks installer are distinct questions.

## Exact missing input

An authoritative CK-5200 installer binary or a vendor-documented recovery procedure for a non-booting application would enable further assessment.
A documented method still requires separately authorized validation before it can satisfy R3.
Another application-only firmware file, a successful stock update, or an app-side retry loop would not resolve the missing evidence.

## Alternate extraction routes checked after the initial audit

### USB control endpoint, not the companion dispatcher

The earlier parser audit covered the companion and iAP2 paths. This follow-up traced the separate USB control endpoint in V122.
The goal was a flash-read route that could obtain the installer while stock still runs, without changing firmware.
The following are static observations from the hash-verified V122 image. No control request was sent to the keyboard.

| Path | Linked addresses | Result |
| --- | --- | --- |
| SETUP dispatch | `0x44EC–0x4526`, request table `0x6704` | Standard requests 0–11 dispatch through a bounded table. Class and vendor requests use separately registered callbacks. |
| Device/configuration/qualifier descriptors | `0x3A58–0x3A76`, `0x4582–0x460E` | Sources are fixed application addresses `0x6560`, `0x6500`, and `0x6574`. Transfer lengths are bounded by descriptor length and requested length. |
| String descriptors | `0x3A78–0x3AA6`, `0x4610–0x4618` | Indices above 5 return null. Index 3 uses the serial formatter. Other supported indices use a bounded pointer table. |
| Dynamic serial | `0x2276–0x22AC` | Formats one fixed word from `0x0800F600` as ten decimal digits in a fixed RAM buffer. It does not accept a flash address. |
| HID report descriptor | `0x3A36–0x3A56`, `0x45EA–0x4608` | Only interface 2 returns the fixed 97-byte descriptor at `0x6580`. |
| HID class reads | `0x3C58–0x3C7E`, `0x42DC–0x432A`, table `0x66B4` | Interface 2 is checked. GET_REPORT uses a callback, GET_PROTOCOL returns constant 1, and other unsupported requests stall. |
| HID callbacks at initialization | `0x3D46–0x3D4A`, `0x432C–0x4336` | Both report callbacks are initialized to null. No alternate report reader was identified in this path. |
| Vendor request callback | `0x3D36–0x3D40`, `0x467E–0x4680` | Registered callback is `0x3CBC`, a return instruction. This is not a memory-read service. |
| Multi-packet IN transfer | `0x4338–0x439A`, `0x3E28–0x3EB0` | Continues from the selected response pointer and stored total. It does not accept a host-supplied memory pointer. |

**[INFERENCE] These normal control-request paths do not expose installer readback.**
This is not a proof against every malformed-packet exploit or timing fault. No such exploit was developed or tested.

The initialized string table was independently extracted from the `.data` payload at `0x6880`.
Its six pointers are `0x20000044`, `0x663C`, `0x6670`, `0x66A0`, `0x65E4`, and `0x6600`.
Index 3 is overridden by the serial formatter before the table lookup.
Parsing all 96 configuration-descriptor bytes found HID interface 2 and the two known vendor interfaces, including alternate 1 on interface 1.
No DFU interface is advertised by this running application. This says nothing about the unread installer's descriptors.
The existing binary validator passed on V122, with reset target `0x485C`.

### Official iOS app and cached firmware metadata

Apple's public [lookup record](https://itunes.apple.com/lookup?id=6466761655&country=us) identifies `com.clickscompanion.app`, version 3.0.2.
Read-only `devicectl device info apps` confirmed the installed app is version 3.0.2, bundle version 3, with `Container Accessible: false`.
An attempt to list its `Library/Caches` returned `CoreDevice.ActionError error 3`.
The tool's documented container requirements include installation through Xcode. The official app is not marked as a developer app.
Therefore this pairing cannot retrieve that app's firmware cache through the supported data-container operation.
No private app files were obtained and no app was launched or replaced.

An existing local MobileSync backup directory was detected, but the file-reading tool could not read it.
Its contents were not inspected, so this audit makes no claim about whether it contains a Clicks cache or firmware metadata.
No backup was created, and no permissions or certificates were changed.

The archived Android APK was searched again for firmware, bootloader, recovery, and distribution strings.
It exposed the already-known Android manifest URL, not an iPhone manifest or independent recovery service.

The [parallel public-source search](public-recovery-search-2026-10-14.md) records additional artifact and recovery-entry leads.

### Reported `123` + Space combination

The public report suggested this combination could restore a connection on an iPhone 15 model.
The official guide assigns it to iPhone 16 USB-C data mode. The report does not establish dead-application recovery.

V122's Space branch at `0x2E28–0x2E44` checks matrix byte 3, bit `0x20`.
It skips the Space usage `0x2C` when matrix byte 5, bit `0x10` is set.
V121 has the same suppression at `0x2D42–0x2D60`.
The physical `123` attribution remains an inference from the layer logic, not a new physical matrix measurement.

The V122 matrix scanner at `0x24AA–0x26D6` invokes two application callbacks, registered at `0x2A36–0x2A4C`.
Those callbacks are the report builder at `0x2796` and layer/backlight handler at `0x2958`.
The reviewed paths contain no installer jump or boot-mode selection for this combination.
Application startup reaches initialization at `0x22F0` and the main loop at `0x235E` through `0x3AF6–0x3BB8`.
This traced path supplies no independent rescue service. The unread installer could still contain different key handling.

### Public test-app history and WCH example

All advertised refs of `louis-hsu/ClicksFT` were fetched into a temporary bare repository, then removed.
There were 20 commits. Historical object names contained no `.bin`, `.hex`, `.elf`, `.ipa`, PDF, Word, or spreadsheet artifact.
The inspection covered 31 historical blobs selected by firmware, USB, command-table, README, build, or manifest paths.
They exposed no firmware-download URL, A0–A3 implementation, or boot/recovery service in the inspected text.
This extends the [public-source note](public-recovery-search-2026-10-14.md) beyond its original branch-tip inspection.

The local WCH SDK includes an IAP example, but it is not the missing Clicks installer:

- [WCH `iap.h`](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.h) selects application base `0x08005000` and commands `0x80–0x84`.
- [WCH `main.c`](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/main.c) selects command-based entry by default. Its alternate entry tests PA0.
- CK-5200's downloaded application starts at `0x2000` and uses A0–A3 staging commands.

The SDK example therefore cannot establish CK-5200 boot behavior or serve as a verified replacement image.
No example code was executed or installed.

### Stopped HTTPS inspection

[CORRECTION] The stock download URL and archived application images were already known.
Missing installer code was confused with missing firmware downloads. HTTPS inspection is not a next acquisition task.
After temporary approval, a restricted proxy was started, then stopped at Rudi's request. Process exit was confirmed.
No phone proxy/certificate settings changed, no official-app traffic was captured, and no keyboard operation ran.
Temporary host proxy/CA files were created. Their cleanup was not confirmed. No certificate-access retry is authorized.
The separate VLANs remain intact. Do not resume the proxy or permission requests.

The following vendor inquiry is prepared but not sent. The official support page lists `support@clicks.tech`.

> Subject: CK-5200 recovery when the application firmware cannot boot
>
> I have a Clicks CK-5200 for iPhone 15 Pro Max, hardware 1.2.0 and stock firmware 1.2.2.
> The keyboard currently works. I need to understand recovery before attempting custom firmware.
>
> Does this model have a recovery mode that works when the application firmware cannot boot or enumerate over USB?
> If so, what is the documented entry method, and which app or host tool supports it?
> Can recovery run from an iPhone 15 Pro Max without an existing Clicks accessory session?
> Please provide the installer image or technical documentation for the boot region below address 0x2000, if available.
> Does that installer validate a staged image or restore a previous image after a failed boot?
> Does the supported recovery method preserve device identity, configuration, and authentication data?

## Hardware boundary evidence, 2026-10-15

WCH's SDK [datasheet index](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/Datasheet/README.md)
links the [official reference-manual page](https://www.wch.cn/downloads/CH32X035RM_PDF.html).
That page returned no manual body through the text fetch.
The inspected WCH-authored manual, version 1.9, came from this pinned community mirror:

<https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/74c247d6d518052316b35b68ddc82db2a70afc52/datasheet_en/CH32X035RM.PDF>

SHA-256: `c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a`.
The PDF has 261 pages. `pypdf` extracted the text after `pdftotext` was unavailable.
This identifies the exact inspected document, not a verified latest vendor revision.

| Location | Documented rule | Limit of the CK-5200 inference |
| --- | --- | --- |
| Section 1.2, PDF pages 5–6 | Memory-mapped program flash, up to 62 KiB. Separate 3,328-byte system bootloader. | Main-flash reads from application code are plausible. The actual protection and boot configuration remain unread. |
| Section 20.1, PDF page 238 | Main flash `0x08000000–0x0800F7FF`, 256-byte pages. Launcher `0x1FFF0000–0x1FFF0CFF`. | The presumed Clicks installer is in main flash, not the separate factory launcher. Reserved application space is not proof of immutability. |
| Section 20.2.2, PDF page 238 | Read protection automatically write-protects pages 0–7, totaling 2 KiB. Page write protection uses 2 KiB units. | This does not establish write protection for the entire presumed 8 KiB Clicks region. |
| Section 20.2.2, PDF page 238 | Releasing read protection automatically erases the user area. | Do not change protection. The text does not prove arbitrary CPU readback under this keyboard's actual configuration. |

Fresh offline V122 disassembly also confirms:

- Startup at `0x485C–0x48CA` sets `gp`, stack, initialized RAM, BSS, and `mtvec`.
- The [stock decode](STOCK_DECODE.md) records application USB/iAP2 and authentication routines, including `0x51CC` and `0x515C`.
  Those application routines do not establish an equivalent service in the unread boot stage.
- Its initialized-data source is `0x6880`. The file ends at `0x68D7`.
- The command-08 branch at `0x3258` enters `0x335E`. That handler is a possible offline redirection candidate, not a certified safe insertion point.
- The application capacity remaining below `0x8A00` is `0x2128` bytes. This does not establish installer acceptance of a larger image.
- The checked images expose neither original reset selection nor boot-stage marker consumption, copy order, validation, or fallback.

Existing host checks rerun on 2026-10-15 passed: 15 iAP2 session cases, four authentication cases,
five USB protocol tests, and the complete staging/fault harness using the images listed above.
Both stock hashes and binary validation passed. V121 resets to `0x4812`, V122 to `0x485C`.
These checks use mocked transport/flash. They do not establish physical authentication, installer execution, or independent recovery.

## Execution limits

This audit used local reads, offline disassembly, existing host harnesses, public HTTP GET requests, and read-only iPhone app metadata requests.
The later stopped proxy attempt is recorded above. It produced no official-app traffic.
No keyboard command, firmware write, reset, ISP operation, protection change, or SDI investigation ran.
No application or firmware implementation changed. Physical recovery and device runtime checks were not run.
