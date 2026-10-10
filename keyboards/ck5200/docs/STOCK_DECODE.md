# Stock firmware decode record

This is the tracked, reviewable record of the reverse engineering behind the
custom firmware's Apple-session stack, physical keymap, and backlight. The
working scratch directory (`.stock/audit/`, gitignored) holds the same
material plus derived files; this document is the durable copy. Everything
here was derived statically from the two vendor images archived in
`.stock/` (see hashes in [USB_AND_IPHONE.md](USB_AND_IPHONE.md)); no
hardware was opened and no dev board was used.

Input images (SHA-256):

| File | Size | SHA-256 |
| --- | --- | --- |
| `iKeyboard_CK-5200_V121_120.bin` | 18572 | `1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa` |
| `iKeyboard_CK-5200_V122_120.bin` | 18648 | `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8` |

Application base `0x2000`, region limit `0x6a00` (ends `0x8a00`).

Toolchain: wrap the `.bin` in an ELF (one `.text` section, base `0x2000`)
and disassemble with Homebrew LLVM:

```sh
llvm-objdump -d --triple=riscv32 --mattr=+m,+a,+c,+xwchc stock.elf
```

The `+xwchc` attribute decodes the WCH compressed instructions; without it
objdump mislabels them as floating point. The wrapper script and the
extraction programs for the generated artifacts live in
[tools/stock_audit/](../tools/stock_audit/). Linked addresses below refer to
V122.

## Public references (cross-check skeleton)

- **wiomoc/iap2** (Python): full link layer with tests. Header struct
  `>HHBBBB` plus one checksum byte; `IAP2_MARKER = FF 55 02 00 EE 10`.
- **usenocturne/iap2-rs** (Rust): working MFi accessory side with EA
  sessions and a pluggable auth-coprocessor trait.
- **zhm-nico-nico-ni/Apple-MFi-IAP2**, **yif-hong/Apple-MFI-AIS-R29**:
  Apple Interface Specification ports on GitHub.
- **JJTech0130's iAP2 Wireshark dissector** (gist `78527c60...`): message catalog.
- USENIX `vehiclesec25-won.pdf`: link/session layer writeup.

Cross-check result: the stock link layer matches wiomoc's semantics exactly
(same header, flags, checksums, marker, detect to negotiate to normal flow).

## USB descriptors (byte-exact from the image)

- Device `352e:2306`, `bcdDevice 0x0122`, iMan 1, iProd 2, iSer 3.
- Config, 96 bytes: interface 2 = HID boot keyboard, EP `0x81`/`0x01`
  interrupt 64 B interval 1; interface 0 = `FF/F0/00`, EP `0x82`/`0x02`
  bulk 64 B (iAP2 link, iInterface 4); interface 1 = `FF/F0/01`, EP
  `0x83`/`0x03` bulk 64 B only at alternate 1 (companion data, iInterface 5).
- Strings, UTF-16LE in image: `0x65e3` "iAP2 Interface", `0x6600`
  "com.clickscompanion.protocol", `0x663b` "Clicks Technology Limited",
  `0x6671` "Clicks Creator Keyboard", `0x66a0` serial "190200001".
- ASCII constants also in image: "CK-5200", "1.2.2", "1.2.0",
  "79Q5CGN6JK", "020c3648d17f4624", "com.clickscompanion.app".
- HID report descriptor, 97 bytes at `0x6580`: 4KRO boot layout plus three
  1-bit consumer usages (`0x01AE`, `0x029D`, `0x00CF`).

## Session architecture (linked addresses)

Outer context: `+0x0` flags byte, `+0x1` connected flag (`0x61ea`),
`+0x2` outer state (0 off, 1 attach, 3 link active), `+0x4` attach backoff,
`+0x6` session object (`0x63f8` init), `+0x30` transport state machine
(`0x5f80`/`0x5f18`), `+0xc8` watchdog reset to 2500 on activity, `+0xc4`
the app callback (`0x6298`).

Functions: `0x6242` poll; `0x6150` EP2 receive router; `0x6178` pre-session
parser; `0x61ac` reject (18-byte template at `0x64ec`); `0x626a` attach send
(6-byte template, image `0x68d0`, 475-tick retry, 50 on failure);
`0x61c6` the 1 ms tick; `0x629e`/`0x62cc` EP3 gates (outer state 3);
`0x470a` EP2 send; `0x46d2` EP2 receive.

Inner machine at `0x5f9e`: ten states, jump table `0x6754`, pending op at
`+0x84`, next state at `+0x80`, retry at `+0x8c`. State 1 runs pending ops;
state 2 backs off 10 ticks; state 3 resends the sync every 1000 ms up to 30
sends; states 5-7 relay the auth challenge; states 8-10 close the session;
state 0 pumps the link ring (`0x4fa8`, eight slots).

## Link packets (byte-exact from `0x542a`)

Header, 9 bytes: `FF 5A | length BE16 | control | seq | ack | session |
checksum = -(sum of first 8)`. Payload checksum `-(sum of payload)`.

LinkSyncRequest, 23 bytes: control `0x80` (or `0xC0` restating after their
SYN), payload `01 05 10 00 07 D0 01 F4 1E 03 0A 00 01 <ck>`: version 1,
max outgoing 5, max length 4096, retransmission timeout 2000 ms, ack
timeout 500 ms, 30 retransmissions, ack every 3, session id 10.

Received parsing at `0x5bec`: control `0x80` SYN (validate echoed
parameters, restate ours as SYN|ACK on mismatch, bare ACK on match);
`0x40` ACK; `0x20` EAK; `0x10` RST (restart the link); `0x08` flag.
Data packets require session id 10 and dispatch on the message type:
`0x1D00` RequestIdentify, `0x1D02` session open, `0xAA00` certificate
request, `0xAA02` challenge, `0xAA04` auth accepted, `0xAE01` power
update, `0xEA00` EA open, `0xEA01` session end. (An early working note
labeled the `0xAAxx` family `0x4Axx`; that was a lui-base arithmetic error
in the notes, corrected against the instructions.)

Control messages: `40 40 | len BE (body+6) | type BE | KVPs | -(sum)`,
KVP = `entry_len BE (len+4) | id BE | value`.

## Identify reply (`0x1D01`, stock `0x5734`)

Sixteen parameters, generated from image bytes `0x677c..0x6862` into
`firmware/platform/ch32v20x/iap2_ident_data.h`: name, model `CK-5200`,
manufacturer, serial, firmware `1.2.2`, hardware `1.2.0`, capability
blobs, `100`, the 49-byte EA blob carrying `com.clickscompanion.protocol`,
token `79Q5CGN6JK`, language `en`, the `IAP2-X` and `HID-X` blobs, and
`020c3648d17f4624`.

## MFi auth chip (bit-banged I2C, not SPI)

- Bus: GPIOA PA13 = SCL, PA14 = SDA (the SWD pins, repurposed). Register
  endpoints `0x40010804`/`0x40010808`/`0x40010810`; SDA drive/release via
  CFGHR bits 24..27. Chip address byte `0x22` write, `0x23` read, with a
  20-failed-peek fallback toggling to `0x20` (stock `0x62ea`).
- START `0x4ff6`, STOP `0x5048`, ACK read `0x5090`, send byte `0x50ec`,
  delay `0x4fec`. Transactions `0x515c`/`0x51cc`, three retries each.
- Selectors: `0x21` write challenge, `0x10` command and status, `0x11`
  read BE16 response length, `0x12` read response, `0x30` peek pending
  message length, `0x31+cursor` read 128-byte chunk.
- Flow: phone `0xAA02` challenge -> ACK, I2C write (`0x21`), command
  (`0x10`), status poll until `& 0x70`, read length (`0x11`), read
  response (`0x12`), reply `0xAA03`. The certificate streams back as
  `0xAA01` chunks. Stock copies the challenge shifted by one byte
  (KVP header byte 3 first), capped at 32 bytes; the port reproduces this.

## Session flow

attach (`FF 55 02 00 EE 10`, 475 ms) -> echo -> link sync (1000 ms resend,
budget 30) -> SYN exchange -> ACK -> identify `0x1D01` -> certificate
request `0xAA00` -> bare ACK plus streamed `0xAA01` chunks -> challenge
`0xAA02` -> auth relay -> `0xAA03` -> `0xAA04` accepted -> session open
`0x1D02` -> StartPowerUpdates `0xAE00` and status `0xAA03` -> EA open
`0xEA00` -> raw companion traffic on EP3 (outer state 3) -> the existing
`0x02` dispatcher. Reject template: `ff 55 0e 00 13 ff*12 eb`.

## Keymap (stock `0x2b1c..0x3010`)

The stock maps every electrical crossing inline in its HID builder.
Crossing = matrix byte r (PB row), bit b (PA bit); the custom firmware's
scan uses the same indexing.

| r/b | base | SYM | cursor | notes |
| --- | ---- | --- | --- | --- |
| 0/01 | q | 1 | | |
| 0/02 | u | 7 | | |
| 0/04 | k | ' | Down | |
| 0/08 | Backspace | | | |
| 0/10 | Enter | | | extra behavior keyed on layer byte `0x120` |
| 0/20 | | | | consumer bit 2 |
| 1/01 | w | 2 | Up | |
| 1/02 | i | 8 | Up | |
| 1/04 | d | ; | Right | |
| 1/08 | l | ' | Right | |
| 1/10 | v | / | | |
| 1/20 | chord with 5/10 | | | consumer bit 1 |
| 2/01 | e | 3 | | |
| 2/02 | o | 9 | | |
| 2/04 | f | ; | | |
| 2/08 | none | | | spare crossing |
| 2/10 | b | 1 | | |
| 2/20 | | | | Left GUI (Command) |
| 3/01 | r | 4 | | |
| 3/02 | p | 0 | | |
| 3/04 | g | 9 | | |
| 3/08 | z | configurable `0x11e` | | shift interplay at `0x2df4` |
| 3/10 | n | , | | |
| 3/20 | Space | | | |
| 4/01 | t | 5 | | |
| 4/02 | a | - | Left | |
| 4/04 | h | 0 | | |
| 4/08 | x | 7 | | |
| 4/10 | m | . | | |
| 4/20 | | | | Left Ctrl |
| 5/01 | y | 6 | | |
| 5/02 | s | / | Down | |
| 5/04 | j | Clicks key (`0x11f`) | Left | |
| 5/08 | c | 2 | | |
| 5/10 | sticky companion | | | one-shot SYM mechanics at `0x2876` |
| 5/20 | | | | consumer bit 0 |

[UNVERIFIED] the SYM-value semantics of the second/third-row crossings and
the three consumer usages. The custom firmware keeps the stock base and
SYM values, moves the cursor arrows to a held layer, and maps one crossing
to consumer Eject (`0x00B8`) for the iOS on-screen-keyboard toggle.

## Backlight (TIM1, not SPI1)

An earlier note read `lui 0x40013` as SPI1 (`0x40013000`); the actual base
is `0x40012C00` = TIM1. Three PWM channels, CH1/2/3 on PA8/PA9/PA10
(stock init `0x306a`): PSC 0, ARR 6000, brightness duty
`(0x100 - raw) * 6000 >> 8` on all three CCRs (stock `0x3010`), raw byte
inverted (0 = brightest), channel gates in CCER (`0x3050`/`0x4ef8`),
MOE (`0x4eba`), ARPE and CEN (`0x4ed0`/`0x4ea2`). Stock disables the
channels at boot pending an activity state machine that was not decoded;
the port keeps them on.

## Remaining unknowns

- Dispatch tails `0x5e9a`/`0x5e9e`/`0x5eb8`/`0x5ec6` delivery details.
- Serial getter `0x2276` source, timing global `gp-0x7e0` init.
- The stock backlight activity-based on/off state machine.
- Consumer usage semantics `0x01AE`/`0x029D`/`0x00CF`.
- Multi-USB-packet received link frames (stock has the same single-packet
  limitation).

## Corrections issued during the decode

1. The attach constant `ff 55 02 00 ee 10` IS in the image at file offset
   `0x48d0` (linked `0x68d0`); an intermediate note wrongly "corrected"
   this by confusing file offset `0x28d0` (code) with `0x48d0`.
2. The auth family is `0xAA00..0xAA04`, not `0x4Axx` (lui base arithmetic).
3. The auth bus is bit-banged I2C on PA13/PA14, not SPI1.
4. The backlight peripheral is TIM1 (`0x40012C00`), not SPI1
   (`0x40013000`).
5. The sync resend budget is 30 (the stock compares a byte counter to the
   max-retransmissions low byte), not 798.
