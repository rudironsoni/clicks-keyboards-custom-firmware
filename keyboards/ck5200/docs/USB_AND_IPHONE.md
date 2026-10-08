# USB and iPhone acceptance

The target is the Clicks case for **iPhone 15 Pro Max**. Desktop USB operation is a bring-up check, not completion. iOS operation remains unverified until the custom firmware runs on that phone and passes the checks below.

## Device evidence, 2026-10-08

Descriptor reads on the connected case returned `352e:2306`, `bcdDevice=0x0122`, configuration 1, attributes `0xA0`, and 100 mA advertised power.

- HID interface 2, interrupt endpoints `0x81` and `0x01`, 64-byte packets.
- Vendor interface 0, class/subclass/protocol `FF/F0/00`, bulk endpoints `0x82` and `0x02`.
- Vendor interface 1, `FF/F0/01`, no endpoints at alternate setting 0; `0x83` and `0x03` at alternate setting 1.

A passive read from `0x82` returned `ff 55 02 00 ee 10`. A later stock read-only version query returned the same packet:

```text
USB OUT 0x02: 02 03
USB IN 0x82: ff 55 02 00 ee 10
version_query_not_verified: received Apple accessory handshake ff 55 02 00 ee 10, not an update reply; stock session setup is unverified, stopping without retry
```

No A1/A2/A3/A0 commands were sent during these checks. The version query handler in stock file offsets `0x196e..0x1998` constructs a constant response. The unrelated handshake is not a successful version or update response.

[INFERENCE] This handshake likely caused the reported six-byte flash error, but the original error did not preserve the bytes. [USENIX iAP2 research](https://www.usenix.org/system/files/vehiclesec25-won.pdf) identifies this sequence as an accessory handshake. It also exists at offset `0x48d0` in the verified stock image.

## Official Android updater evidence

The [official Clicks download page](https://app.clicks.tech/en/docs/pixel-get-started) links to `https://xinyi1.clicks.tech/app-download/app-prod-release.apk`.

Downloaded APK SHA-256:

```text
fc60df6c69f0fb142892b9a8b531c60fb439b8d7e37bbde01886eea296499740
```

JADX 1.5.6 produced readable code for the relevant classes. Whole-APK decompilation exited 3 with 20 errors, so this is partial static analysis, not a successful execution of the app.

- `L0.a.c(UsbDevice)` selects `getInterface(1)`, endpoint index 1 for OUT and index 0 for IN, then claims the interface. This is an index in Android's descriptor list, not necessarily USB interface number 1.
- `L0.a.f(byte[])` writes with a 1000 ms timeout. `L0.a.d(int)` reads once, returning the allocated array rather than the actual received byte count.
- `T0.o.b(int)` sends A1 with a four-byte big-endian size. `T0.o.c(byte[])` sends 32-byte A2 chunks and uses reply bytes 4..7 as the next offset. `T0.o.a(int)` sends A3.
- Those update methods check reply command bytes but do not check the device status byte. This project retains the stronger status check.

These methods support the inner update packet format. They do **not** prove raw Android USB transactions work against the iPhone firmware. No automatic handshake-skipping, alternate endpoint probing, or guessed mode command was added.

## Current transport behavior

`tools/ck5200_usb.py inspect` reads descriptors without SET_CONFIGURATION, driver detach, or interface claim. `inspect --query-version` additionally claims the vendor interface and sends only `02 03`.

The transport logs raw OUT and IN bytes. It rejects the known accessory handshake instead of parsing or ignoring it. Stock-device flashing is blocked before A1 because its session remains unsupported. The direct Python flash path is retained only for the experimental custom updater reporting `bcdDevice=0x9001`; that descriptor is a routing check, not proof of recovery or image compatibility.

## Required iOS checks

### Stock iOS session and read-command investigation, 2026-10-08

Rudi's physical iPhone 15 Pro Max screenshots at 14:09 and 14:10 show the locally built Clicks Inspector app discovering `Clicks Creator Keyboard`, model `CK-5200`, hardware `1.2.0`, firmware `1.2.2`, and `com.clickscompanion.protocol`. The second screenshot shows `Open on connection 40297598`. In this app, that state requires both EASession streams to report open. Later screenshots at 14:39 through 14:43 show all seven fixed reads completing on connection `40297599`.

| Read | Displayed payload | Interpretation |
| --- | --- | --- |
| Version `0x03` | Hardware `01 20`, firmware `01 22` | Matches the audited stock version reply. |
| Brightness `0x84` | `ff` | Raw value 255. |
| Backlight delay `0x86` | `4e 20` | Big-endian 20000 milliseconds, or 20 seconds. |
| Battery idle interval `0x88` | `00 b4` | Big-endian 180; time unit remains unverified. |
| Raw setting `0x8a` | `00` | Raw value 0; full meaning remains unverified. |
| Raw setting `0x8c` | `21` | Raw value 33; full meaning remains unverified. |
| Status flags `0x8d` | `00` | Both returned bits are clear. |

Evidence is Rudi's supplied result screenshots, not a separate USB packet capture. The installed parser displays `Completed` only after validating the expected length, response type, echoed command, and zero status. This verifies the seven request/reply paths on this stock case. Repeated reads, disconnect and timeout behavior, memory readback, custom firmware, and recovery are not established by these screenshots.

The stock image and APK were rehashed locally. Their hashes match the values above and in [HARDWARE.md](HARDWARE.md). The official Android app is supporting evidence for packet structure, not a substitute for checking the iPhone firmware. Its newer commands `0x11`, `0x12`, `0x8f`, `0x91`, and `0x93` must not be copied into the CK-5200 reader merely because they appear in the APK. The official iOS app binary has not been obtained or analyzed.

The application command dispatcher starts at linked address `0x3216`, file offset `0x1216`. It reads the declared length at byte 0 and command at byte 1. Replies contain total length, `0x02`, echoed command, status, then payload. A success status is zero. The APK's `T0.a.m` checks the same header and takes payload bytes from index 4 up to the declared length. Its USB reader allocates 64 bytes without returning the actual received length, so that reader is not suitable for copying into an iOS byte stream.

These fixed requests have read-only handlers in this exact stock application:

| Request | Reply length | Handler linked address | Returned data |
| --- | --- | --- | --- |
| `02 03` | 8 | `0x396e` | Constant `08 02 03 00 01 20 01 22`. Hardware bytes precede firmware bytes. |
| `02 84` | 5 | `0x35da` | One byte from configuration offset `0x11d`, brightness value. |
| `02 86` | 6 | `0x3634` | Two bytes, big-endian, from configuration offset `0x124`. The APK treats this as a millisecond interval. |
| `02 88` | 6 | `0x36c6` | Two bytes, big-endian, from configuration offset `0x126`. |
| `02 8a` | 5 | `0x3706` | One byte from configuration offset `0x11f`. Preserve the raw value; its full meaning is unverified. |
| `02 8c` | 5 | `0x375c` | One byte from configuration offset `0x11e`. Preserve the raw value; its full meaning is unverified. |
| `02 8d` | 5 | `0x333e` | Two runtime flags extracted from bits 4 and 5 of the state word. This is not stored firmware. |

The settings handlers load fixed fields and fill the reply buffer. They do not accept a read address or length and do not call flash writers. In particular, `0x82` is not another getter: it takes a brightness argument and calls the brightness routine. The adjacent setter commands are excluded.

The receive callback at `0x2208` passes the incoming buffer and byte count to `0x3216`. Initialization at `0x2340` registers that callback through `0x4776` and `0x6298`. Endpoint-3 receive code at `0x4724` reads up to 64 bytes and passes them to `0x629e`, which invokes the callback unchanged when transport state is 3. The reply follows `0x2266` to `0x4788`, `0x62cc`, and the endpoint-3 send wrapper `0x475c`. The completed phone reads confirm that the implemented inner request bytes work through EASession. No host-generated iAP handshake or guessed framing is added. This does not validate raw Mac USB updates.

The dispatcher also contains settings writes, command `0x08`, and `0xa0` through `0xa3`. Settings writes and update/reset commands are excluded. A later diagnostic build adds one fixed `0x08` probe, described below. The default branch constructs an error reply with status `0xff`. No arbitrary memory-read operation was identified in this dispatcher. That conclusion does not establish the behavior of an unread bootloader or an undocumented service interface.

To reproduce the address inspection with the installed compiler tools, from `keyboards/ck5200`:

```sh
.toolchains/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-objdump \
  -D -b binary -m riscv:rv32 --adjust-vma=0x2000 \
  .stock/iKeyboard_CK-5200_V122_120.bin
```

The generic disassembler mislabels some WCH compressed encodings as floating-point instructions. Do not treat those printed mnemonics as established behavior. The fixed getter table above rests on standard loads, stores, branches, and explicit response constants; the transport state checks also contain compressed instructions and require runtime confirmation.

### Backup coverage after the session test

| Region | Bytes extracted from this keyboard | What is available |
| --- | --- | --- |
| Clicks boot/install component below `0x2000` | 0 | No read command or dump. |
| Installed application | 0 | A separately downloaded, hash-checked V122 application, not a device readback. |
| Persistent settings pages, staging, and additional main flash | 0 | Fixed field reads can expose selected values only, not the underlying pages. |
| WCH system region, vendor region, and option bytes | 0 | No established access through the app. |
| Any separate authentication or storage chip | 0 | Physical components and contents remain unidentified. |

There is no full backup to compare or restore. Reading the fixed fields twice can check those replies, but cannot establish full-region coverage. The next full-backup requirement is a proven memory-read service for this exact model, or physical debug access under the conditions in [RECOVERY.md](RECOVERY.md). An app cannot recover bytes that the accessory protocol never exposes.

### Bounded service probes

Rudi authorized further app-based investigation after the seven reads passed. Two concrete packets are included in the later diagnostic build. Rudi's screenshots at 15:16 and 15:17 confirm both probes completed on connection `40297601`, model CK-5200, hardware `1.2.0`, firmware `1.2.2`. The checksum probe displayed an acknowledgement with no returned data. The newer user-key probe displayed unsupported status `ff` and no keymap. These screenshots show parsed results, not independent wire captures; the parser requires the exact expected header/status and, for `0x8f`, four zero payload bytes. Both observed outcomes match the static analysis below.

| Probe | Exact request | Expected reply | Question tested |
| --- | --- | --- | --- |
| Checksum service | `05 08 00 00 00` | `04 02 08 00` | Does the real unit expose checksum output that differs from the downloaded V122 handler? |
| Newer user-key read | `06 8f 00 00 00 00` | `08 02 8f ff 00 00 00 00` | Does the real unit reject the Android app's user-key getter as the audited binary predicts? |

The `0x08` handler at linked `0x335e..0x3408` accepts declared lengths 5 through 19. It copies a fixed 34-byte flash table at `0x64c8` into a local stack buffer. The chosen five-byte request has selector zero, so it skips the indexed XOR operation and uses only its two supplied zero data bytes. The raw instructions `3ba9` at `0x33d6` and `33a1` at `0x33e8` decode as RV32 C.JAL calls to the CRC routine at `0x3130`. The handler stores the calculated CRCs in reply offsets 4 through 7, but writes reply length 4 at `0x33f2`. The outer callback sends that declared length. Thus the static path returns only an acknowledgement, not the checksum values. It is not an arbitrary-address checksum oracle.

The earlier disassembler labels `0x3230` and `0xb6f0` as floating-point instructions. Matching the [LLVM WCH compressed-instruction encoding tests](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.3/llvm/test/MC/RISCV/xwchc-valid.s) identifies byte load `qk.c.lbu a2,3(a2)` and byte store `qk.c.sb a2,15(a3)`. In this request path the store targets the bounded local work buffer. The checked handler has no settings, flash, or peripheral writes. This finding applies to this exact packet, not arbitrary malformed lengths or selectors.

Command `0x8f` takes the default branch at `0x328a`. That branch fills an eight-byte reply with type 2, echoed command, status `ff`, and four zero bytes. The probe supplies four zero argument bytes because the dispatcher preloads those bytes even before rejecting this command. It does not call a flash writer or modify configuration. A nonmatching response is retained as unexpected evidence and closes the session; it is never automatically retried with another command.

Fresh searches for the exact model, firmware filename, protocol string, and Clicks firmware reverse engineering found no usable public CK-5200 service-readback implementation in the results checked. Generic WCH tools do not establish a Clicks-app transport. In particular, [wch-web-isp](https://github.com/basilhussain/wch-web-isp#limitations) documents no user-application flash read in the factory protocol, and [wchisp's command definitions](https://raw.githubusercontent.com/ch32-rs/wchisp/main/src/protocol.rs) distinguish verification from separate configuration/data reads. These sources do not establish a way to enter or tunnel the factory protocol through EASession. No ROM, reset, update, or unprotect request was sent.

The diagnostic build and extended existing tests passed locally. Wireless installation and launch eventually succeeded after an initial CoreDevice error 4016 and a 30-second timeout. CoreDevice reported a connected local-network tunnel, but a later screenshot request again failed with error 4016. The subsequent on-phone screenshots verify both diagnostic outcomes. Neither probe returns firmware bytes or establishes a full-backup method. These results close these two specific leads; they do not prove the absence of every undocumented interface or packet-parser defect.

### Custom firmware acceptance

The successful stock session does not transfer to the QMK image. `firmware/usb/usb_descriptors.c` currently exposes one vendor interface on `0x02/0x82` and a keyboard HID interface. It omits the stock session data interface on `0x03/0x83`. Copying the protocol string alone would not implement session negotiation. The inspector therefore accepts read commands only for the audited stock model and versions.

The QMK path remains conditional on independent recovery, preservation of the boot/install region and settings layout, and a verified Apple accessory implementation if companion-session support is retained. The corrected linker already uses application base `0x2000` and size limit `0x6a00`. Keep that fix. The diagnostic matrix keymap still needs a physical key map before usable layers can be built. None of these requirements is resolved by a stock settings reply.

Every item below is **NOT VERIFIED** on custom firmware:

1. Cold boot and enumeration when attached to the iPhone 15 Pro Max.
2. Physical key mapping, key down/up, rollover, Shift/Control/Option/Command, and stock special-key behavior.
3. Intended base layout and layers, with no stuck keys or missed releases.
4. Lock, unlock, sleep, wake, disconnect, and reconnect.
5. Backlight brightness and timeout behavior.
6. Charging through the case while typing, plus USB behavior with the charger attached or removed.
7. Return to the exact stock application and normal phone operation.

The current image has no Apple accessory session, no backlight driver, and only a diagnostic matrix keymap. Stock includes `IAP2-X`, `com.clickscompanion.protocol`, and `com.clickscompanion.app`. Plain HID support on this case must be tested rather than assumed. Companion-app control requires its own verified protocol if preserving it is necessary. Do not claim iOS compatibility from Android analysis or Mac enumeration.

## Exploration only: stock customization and a companion product

Rudi requested two independent explorations while the fixed-read app was being built. Neither exploration changed code or sent keyboard commands. Custom firmware remains the main goal.

The audited V122 application dispatcher has no identified per-key mapping or user-defined layer command. Its configurable values support existing behavior rather than a downloadable matrix keymap. [INFERENCE] A companion app using this command interface cannot create arbitrary system-wide symbols or layers without additional keyboard-side support. This does not rule out an undocumented interface or different firmware.

The Android APK has three configurable user keys, with getters `0x8f/0x91/0x93` and setters `0x8e/0x90/0x92` in `T0.n`. Classes `Q0.s`, `Q0.t`, and `Q0.u` list fixed presets such as Tab, Ctrl+Space, Alt combinations, Gemini, and minus. Those opcodes fall through the exact CK-5200 V122 dispatcher's unsupported-command path. They are not evidence of iPhone 15 remapping support, and none was added to the inspector.

The [official Clicks v1.2 feature notes](https://discover.clicks.tech/clicks-keyboard-app-v12-introduces-cursor-mode-clicks-key-customization-and-more) describe a built-in cursor mode, a Tab/Ctrl choice for the Clicks key, a currency-symbol choice, and backlight controls. The notes explicitly say availability differs by model. Those are useful fixed functions, but the article does not describe an arbitrary keymap or layer editor. The exact bit/value mapping on this case still needs evidence before any setter is tested.

Apple's [UIKeyCommand](https://developer.apple.com/documentation/uikit/uikeycommand) handles shortcuts in an app's responder chain. [INFERENCE] That API does not supply a global replacement for the physical keyboard's output in other apps. A software keyboard extension or foreground text tool would not meet the requested hardware-layer behavior. User-configured modifier keys and accessibility shortcuts are separate iOS features, not new case firmware layers.

Product assessment: a stock companion could offer local settings profiles, comparisons, and diagnostics for proven settings, but would overlap the official app. A companion for custom firmware could edit keymaps and layers that the keyboard itself applies across apps. That is the better fit for the original goal, conditional on recovery and a working iPhone-compatible firmware. This is an exploration result, not an implemented product or a change in project scope.

The successful development-signed EASession is not proof of App Store acceptance or manufacturer support. Apple's [External Accessory documentation](https://developer.apple.com/documentation/externalaccessory/) says the manufacturer decides which third-party apps may communicate. [UNVERIFIED] Vendor authorization, model coverage, and acceptance of a distributed app remain open. No vendor outreach or App Store submission was made.
