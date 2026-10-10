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

### Vendor firmware distribution, 2026-10-08

A no-open search for a published bootloader or full image found none. The Android app's DEX strings contain the only firmware manifest reference, `https://xinyi1.clicks.tech/firmwares-a.json`. It lists `CA-1100`, `CA-2100`, and `CA-2200` (Pixel models) with `{model, firmwareRevision, file, sha256sum}` entries; each `file` is an 18 KB-class application image. No bootloader, installer, or full image appears in it.

The bucket behind `xinyi1.clicks.tech` is S3-style: unknown keys return an `AccessDenied` XML error and `?list-type=2` listing is denied, so only exact keys can be checked. For `CK-5200/` exactly two keys exist, both application images, now both archived locally in `.stock/` (ignored by Git):

| File | Size | SHA-256 |
| --- | --- | --- |
| `iKeyboard_CK-5200_V121_120.bin` | 18572 | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |
| `iKeyboard_CK-5200_V122_120.bin` | 18648 | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |

`V123` and `V124` keys, an iOS-model manifest under predictable `firmwares-i.json`-style names, and boot-image names under the `iKeyboard_` convention all return `AccessDenied`. The [iPhone get-started page](https://app.clicks.tech/en/docs/iphone-get-started) links only the App Store app. The App Store record for `com.clickscompanion.app` (Clicks Keyboard, id 6466761655, iOS 3.0.2) reports compatibility with iPhone, iPad and iPod touch only: there is no Mac build, so its firmware-fetch URL cannot be discovered by running it on this Apple-silicon Mac. Web searches found no public Clicks firmware dump or bootloader. The app's release notes describe settings backup and restore through a Clicks account; that is preferences, not firmware.

Remaining no-open discovery route: capture the official iOS app's update-check traffic on the physical iPhone with a locally trusted proxy certificate. That would reveal the iPhone manifest URL and settle definitively whether any boot or full image is published for `CK-*` models. Given the Android manifest pattern, the expected result is application images only. No such capture was made here.

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

Fresh searches for the exact model, firmware filename, protocol string, and Clicks firmware reverse engineering found no usable public CK-5200 service-readback implementation in the results checked.

## Apple accessory session (MFi) port status, 2026-10-10

The custom firmware now carries a faithful port of the stock Apple session stack, recovered entirely from the static analysis of the V122 image (no case opening, no dev board). Stock addresses below refer to linked addresses in `iKeyboard_CK-5200_V122_120.bin`.

[CORRECTION] An earlier note in this file called the attach constant's location wrong. It was itself wrong: the six-byte attach template `ff 55 02 00 ee 10` IS in the image at file offset `0x48d0` (linked `0x68d0`); the "correction" had confused file offset `0x28d0` (code) with `0x48d0`. The 18-byte pre-session reject template lives at file `0x44ec` (linked `0x64ec`) as originally documented.

### What the stock stack is (all decoded, byte-exact)

- Outer transport (stock `0x6242`, `0x6150`): attach `FF 55 02 00 EE 10` on EP2 every 475 ms; the phone's echo re-inits the link machine and enters outer state 3; anything else pre-session gets the 18-byte reject from `0x64ec`.
- iAP2 link layer on EP2 (stock `0x5bec`, `0x542a`, `0x5534`, `0x5502`): `FF 5A` header (length BE, control, seq, ack, session id, header checksum `-(sum)`), LinkSyncRequest payload `01 05 10 00 07 D0 01 F4 1E 03 0A 00 01 <ck>` advertising max_outgoing 5, max_len 4096, retransmit 2000 ms, ack 500 ms, 30 retransmissions, ack-every-3, control session id 10. Received SYNs are validated against those parameters; mismatch re-states ours as SYN|ACK (the common case, since the phone sends its own parameters).
- Control messages (stock `0x53b0`, `0x53e4`): `40 40 <len BE = body+6> <type BE> <KVPs: entry_len BE, id BE, value> <-(sum)>`.
- Identify (stock `0x5734`): reply `0x1D01` to the phone's `0x1D00`, sixteen parameters generated from image bytes `0x677c..0x6862` (`iap2_ident_data.h`): name, model `CK-5200`, manufacturer, serial, firmware `1.2.2`, hardware `1.2.0`, capability blobs, `100`, the EA blob carrying `com.clickscompanion.protocol`, token `79Q5CGN6JK`, language `en`, the `IAP2-X` and `HID-X` blobs, and `020c3648d17f4624`.
- MFi authentication relay (stock `0x51cc`, `0x515c`, `0x4ff6`-`0x5350`): the authentication chip is on **bit-banged I2C, not SPI**: GPIOA PA13 = SCL, PA14 = SDA (the SWD pins, repurposed), chip address byte `0x22` write / `0x23` read, with a 20-failed-peek fallback that toggles the address to `0x20`. Selectors: `0x21` write challenge, `0x10` command/status, `0x11` read BE16 response length, `0x12` read response, `0x30` peek pending message, `0x31+cursor` read 128-byte chunk. The phone's challenge arrives as `0xAA02`; the chip's certificate streams back as `0xAA01` chunks; the signed response returns as `0xAA03`; the phone's `0xAA04` marks auth accepted.
- Session open (stock `0x5e9e`): the phone's `0x1D02` sets the open flag and queues StartPowerUpdates `0xAE00` and the status report `0xAA03`; the phone's `0xEA00` opens the EA session; `0xEA01` closes it. The companion protocol then runs raw on EP3 (stock `0x629e`/`0x62cc` gate on outer state 3), where the existing `0x02` dispatcher serves the app unchanged.

### Port files

- `firmware/platform/ch32v20x/iap2.c/.h`: outer + inner state machines, link framing, control-message builders, all stock ops.
- `firmware/platform/ch32v20x/iap2_auth.c/.h`: bit-banged I2C driver and selector protocol.
- `firmware/platform/ch32v20x/iap2_ident_data.h`: generated Identify parameters (extracted from the image, not hand-copied).
- `firmware/usb/usb_descriptors.c`: stock topology: interface 2 HID (EP `0x81`/`0x01`), interface 0 `FF/F0/00` (EP `0x82`/`0x02`, iAP2 link), interface 1 `FF/F0/01` (EP `0x83`/`0x03` at alternate 1, companion data). Stock strings: `iAP2 Interface`, `com.clickscompanion.protocol`, serial `190200001`.
- `firmware/platform/ch32v20x/protocol.c`: EP2 routes to the session stack (with a dispatcher fallback outside a session that keeps the Mac recovery path alive); EP3 routes to the dispatcher while the session is active; SOF-equivalent 1 ms tick drives the retry timers.

### Deliberate deviations from stock (each required or safety-motivated)

1. `bcdDevice` stays `0x9001` (custom routing marker) so the Mac flash tool can tell this firmware from stock. Stock itself changes this field between releases (`0x0121`, `0x0122`), so no host can depend on it.
2. The HID report descriptor stays the standard 6KRO boot layout our QMK host speaks, not stock's 4KRO-plus-consumer layout.
3. Identify parameter 3 (serial) uses the constant `2311000001` from the image; stock reads a runtime buffer whose source was not decoded.
4. The status report uses the image initializer bytes of the runtime global stock reads (`{08 34}`, `{01}`).
5. Outside a session, dispatcher-framed packets on EP2 reach the dispatcher instead of being rejected, preserving the Mac flash/restore path. The phone never sends that framing (its length byte would exceed the 64-byte endpoint), so the Apple path is unaffected.
6. The link machine enters the sync-resend state (stock state 3) after sending the sync, matching the resend machinery and the open iAP2 implementations; the exact stock writer of that transition was not located statically.
7. Build-level: LTO (`-flto=auto`) and the heap-free `sym_defer_g` debounce keep the image inside the 27136-byte application region alongside the session stack.

### Physical keyboard, 2026-10-10

The stock keymap is now fully decoded (the stock maps every electrical crossing inline in its HID builder; the complete 36-crossing table is in `.stock/audit/NOTES.md`), and the custom firmware carries it:

- Layer 0 is the stock base layout, crossing-identical to our matrix scan: QWERTY letters, Backspace, Enter, Space, Command (`Left GUI`) and Ctrl on the stock crossings, three consumer keys, the Clicks key, and the sticky SYM key. Crossing (2, bit 3) is the stock spare and stays empty.
- Layer 1 holds the stock SYM values: the letter keys map to their row numbers (`q..p` to `1..0`) and the remaining crossings carry the stock punctuation.
- Layer 2 carries the stock cursor-mode arrows, reached by holding the Clicks key (`LT(2, KC_J)`: tap is `j`, hold is the arrow layer, matching the 9981-style hold-a-key pattern).
- The sticky SYM key is QMK one-shot layer 1 (`OSL(1)`): tap, type one symbol, back to layer 0.
- The report descriptor now appends a 16-bit consumer field to the 6KRO boot report (10 bytes total), keeping six-key rollover unlike stock's 4KRO plus three consumer bits. One crossing is mapped to consumer Eject (`0x00B8`), which toggles the iOS on-screen keyboard; the other two carry the stock's consumer usages (`0x01AE`, `0x029D`, semantics unverified).

### Backlight, 2026-10-10

The stock backlight is now decoded and ported (it is TIM1 PWM on PA8/PA9/PA10, not SPI as the earlier note guessed): prescaler 0, auto-reload 6000, duty `(0x100 - raw) * 6000 >> 8` on all three channels, MOE/ARPE/CEN per stock. The dispatcher answers the stock-shaped brightness commands: `03 82 raw` sets and persists the byte through the wear-leveled EEPROM, `02 84` reads it back (`05 02 84 00 raw`). The persisted value applies at boot. Deliberate deviation: the channels stay enabled at boot; the stock's activity-based on/off state machine (its auto-off after the configured delay) is future work, so commands `0x86`/`0x88`/`0x8a` remain unimplemented rather than lying with canned replies.

### Verification state

Offline: build passes full ELF/BIN validation; the test suite passes (5 Python tests, the update-protocol and keymap C tests, plus the two host harnesses below); image 23388 bytes, sha256 `dad34db61f2978836b0f6c8a29e72f29d491fda0401bb1d0b45928b47906b12c`. The session bytes were derived instruction-by-instruction from the stock image and cross-checked against the public iAP2 link layer (`wiomoc/iap2`, header/flags/checksums match exactly).

Two host harnesses now execute the ported code directly:

- `tests/iap2_session_test.c` (13 scenarios): compiles the real `iap2.c` against a scripted iPhone and a fake auth chip, asserting every outgoing EP2 packet byte-for-byte. Covers the attach cadence (475 ms retry, 50 ms on send failure), the pre-session reject, the sync request with its 1000 ms resends and 30-send budget, the SYN/SYN|ACK/ACK handshake sequencing, the Identify reply (full parameter walk plus string offsets), link-layer ACKs owed, the certificate stream (128-byte chunk math, 0xAA01 framing), the auth challenge relay including stock's shifted challenge copy, the session open sequence (0xAE00, status 0xAA03), RST restart, the 20-failed-peek address toggle to 0x20, the request ring's drop-when-full (7 usable slots), and a canary test proving the parser cannot read past a received packet.
- `tests/iap2_auth_test.c` (4 scenarios): compiles the real `iap2_auth.c` with host register hooks against a waveform-level I2C slave that decodes the PA13/PA14 bit stream. Verifies START/STOP, MSB-first sampling, address and selector bytes, slave ACKs, master ACK/NACK discipline on reads, and the 3-attempt address-NACK bail.

Building these harnesses caught five real defects in the port, all fixed: empty-body control messages were dropped (stock dispatches them, which matters for `0xAA04`), the sync resend cap was 798 instead of stock's 30, a failed pending op was consumed instead of retried (stock keeps the op slot on failure), the Identify serial parameter was never emitted, and the auth read transaction fell through to the read phase after a failed address phase (stock bails after the retry loop).

NOT VERIFIED on hardware: the first flash remains the first live test. The known open runtime risks: the auth-chip address variant (`0x22` vs `0x20`), I2C bit timing, the EA-open parameter the phone sends in `0xEA00`, and multi-USB-packet received link frames (stock has the same single-packet limitation).

The diagnostic build and extended existing tests passed locally. Wireless installation and launch eventually succeeded after an initial CoreDevice error 4016 and a 30-second timeout. CoreDevice reported a connected local-network tunnel, but a later screenshot request again failed with error 4016. The subsequent on-phone screenshots verify both diagnostic outcomes. Neither probe returns firmware bytes or establishes a full-backup method. These results close these two specific leads; they do not prove the absence of every undocumented interface or packet-parser defect.

### Offline parser audit, 2026-10-08

Rudi directed the focus to extracting the stock firmware. This completes the offline audit of the stock V122 packet parsers that an earlier turn left as "a possible length-check bug in the raw USB parser, no proven leak". No command was sent to the keyboard. The tool is Homebrew LLVM 23.1.2:

```sh
llvm-objdump -d --no-show-raw-insn --triple=riscv32 --mattr=+m,+a,+c,+xwchc stock.elf
```

`llvm-objdump` needs an ELF wrapper around `.stock/iKeyboard_CK-5200_V122_120.bin` (one `.text` section, base `0x2000`, flags alloc+exec); it then decodes the WCH compressed encodings correctly, unlike the xpack `riscv-none-elf-objdump` output reproduced above.

Results, with linked addresses in the verified stock image:

| Check | Location | Result |
| --- | --- | --- |
| Dispatcher length check | `0x3226` | Present. `bltu a2, s3` sends actual count < declared length to `0x358c`, which returns 0 with no reply. The callback at `0x2208` sends nothing when the dispatcher returns 0. |
| Reply lengths | all handlers | Every reply-length store is a `li` immediate 4, 5, 6, or 8 into reply byte 0 at context offset `0x10c`. No handler stores a request-derived reply length. The reply buffer is 8 bytes, `0x10c..0x113`, directly after the 256-byte A2 staging RAM buffer at `0xc..0x10b`. |
| Request echo in replies | all handlers | None. Reply payloads are constants, fixed configuration fields (context offsets `0x11d..0x126`), or runtime flags. The A1/A2/A3 replies carry staging state (`0x4`, `0x8`, `0x2` fields), not memory contents. The `0x08` CRC path writes reply offsets 4..7 but declares length 4, so those bytes are never sent. |
| EP2 pre-session parser | `0x6178..0x6194` | Defect, non-disclosing. It matches request bytes 2..5 against `02 00 ee 10` before checking the received count is at least 6. A short packet compares stale bytes of the 64-byte EP2 stack buffer (`0x46d2`). This can confuse the transport state machine into session mode, but no reply path transmits that buffer. |
| iAP2 link parser | `0x5bec` | Defect, non-disclosing. It requires count > 8 and sync `FF 5A`, then trusts the declared link length (bytes 2..3) and declared session lengths. The packet walkers at `0x5e8a..0x5e92` and `0x5e2c..0x5e88` advance by declared lengths bounded only by a 16-bit value, so a packet whose declared lengths exceed the actual count parses stale stack bytes or walks past the buffer. The only delivery loop copies at most 32 bytes into the fixed EA buffer (link object at transport context + `0x30`, pointer field + `0x4`) and records the real copied count at buffer offset 8, so the dispatcher's declared-versus-actual check still operates on true data. |
| Transport topology | `0x46d2`, `0x4724`, `0x629e` | EP2 (interface 0, endpoints `0x02/0x82`) carries the link and control layer: handshake matching, `FF 5A` link packets, session control, and EA control (`0xea400`). The application callback `0x2208` is invoked only by the EP3 path `0x629e`; EP3 (interface 1 alternate 1, endpoints `0x03/0x83`) delivers raw session-data packets to the dispatcher when transport state is 3. Link replies (`0x5cd6..0x5e26` through `0x4f68`) carry state bits and flash constants at `0x542a`, `0x5b62`, `0x5aea`, and `0x5bc0`. |

Conclusion: the audited V122 application exposes no software-only memory-read path. There is no arbitrary-address getter, no request echo, and no request-derived reply length; the parser over-reads feed request parsing only, and no read data is transmitted back. This closes the application-level extraction lead. The boot/install component below `0x2000` remains unread and unaudited, and no audited command reaches it. Full extraction still requires the physical debug route in [RECOVERY.md](RECOVERY.md) or a vendor service/readback mode from Clicks.

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
