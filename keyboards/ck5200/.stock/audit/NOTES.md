# CK-5200 stock MFi/iAP2 stack: reverse-engineering notes

Working notes for the port. Image: `.stock/iKeyboard_CK-5200_V122_120.bin`
(sha256 8ee86935..., base 0x2000). Disassembly: `audit/v122.dis`
(llvm-objdump, `--mattr=+m,+a,+c,+xwchc`). Range helper: `audit/rg.py`.

## Public references (cross-checked, use as skeleton)

- **wiomoc/iap2** (Python): full link layer + tests. Header struct
  `>HHBBBB` + 1 checksum byte. `IAP2_MARKER = FF 55 02 00 EE 10`.
- **usenocturne/iap2-rs** (Rust): working MFi accessory side, EA sessions,
  pluggable `MfiAuthProvider` (auth coprocessor over I2C).
- **zhm-nico-nico-ni/Apple-MFi-IAP2**, **yif-hong/Apple-MFI-AIS-R29**:
  Apple Interface Specification ported to GitHub.
- **JJTech0130 iAP2 Wireshark dissector** (gist 78527c60...): message catalog.
- **USENIX vehiclesec25-won.pdf**: link/session layer writeup.

Cross-check result: stock firmware matches wiomoc semantics exactly
(same header, flags, checksum, marker, state flow detect->negotiate->normal).
The stock is a licensed-style iAP2 link layer; we reimplement the documented
protocol using stock's exact constants.

## USB descriptors (recovered byte-exact from image)

- Device `352e:2306`, bcdDevice 0x0122, iMan=1 iProd=2 iSer=3.
- Config (96 B): iface 2 = HID boot keyboard, EP 81/01 irq 64B int 1;
  iface 0 = FF/F0/00, EP 02/82 bulk 64B (iAP2 link, iInterface 4);
  iface 1 = FF/F0/01, EP 03/83 bulk 64B, only alt 1 (EA data, iInterface 5).
- Strings (UTF-16LE in image): 0x65e3 "iAP2 Interface", 0x6600
  "com.clickscompanion.protocol", 0x663b "Clicks Technology Limited",
  0x6671 "Clicks Creator Keyboard", 0x66a0 "190200001" (serial).
- ASCII strings also in image: "CK-5200", "1.2.2", "1.2.0", "79Q5CGN6JK",
  "020c3648d17f4624", "com.clickscompanion.app" (used by ident messages).
- HID report descriptor 97 B at 0x6580 (boot layout + 3 consumer usages).

## Architecture (linked addresses)

Outer context (ctx), fields:
- ctx+0x0 flags byte (bit0/1 session params; bit3 session-open indicator
  read by 0x61f4; bit4/5 exposed by stock command 0x8d)
- ctx+0x1 connected flag (0x61ea(a0, a1): set; if 0 -> state=0)
- ctx+0x2 outer state: 0 off, 1 attach phase, 3 link active
- ctx+0x4 16-bit attach backoff counter (tick at 0x61c6)
- ctx+0x6 session object (init 0x63f8; methods 0x6362 0x637a 0x6396)
- ctx+0x30 transport state machine object (init 0x5f80/0x5f18)
- ctx+0x90(in transport obj) packet buffer ptr = outer ctx+0xcc
- ctx+0xc8 32-bit watchdog, reset to 2500 on every link/EP3 packet
- ctx+0xc4 app callback (registered via 0x6298)

Functions:
- 0x6242 poll (state 1: resend attach; state 3: step inner machine 0x5f9e)
- 0x6150 EP2 receive: state 3 -> 0x5bec link parser; else 0x6178 pre-session
- 0x6178 pre-session parser: bytes[2..5] == 02 00 ee 10 -> re-init transport
  (0x5f18 with flag bits 0/1), outer state=3, watchdog 2500; else 0x61ac
- 0x61ac reject: copy 18 bytes from 0x64ec to stack, EP2 send, state=0
  (template: ff 55 0e 00 13 ff ff ff ff ff ff ff ff ff ff ff ff eb)
- 0x626a attach send: 6-byte template from global (init from image 0x68d0:
  ff 55 02 00 ee 10), send len 6 via 0x470a, backoff 475 ticks ok / 50 fail
- 0x61c6 tick (1 ms): decrement ctx+0x4, ctx+0xc8, inner retry 0x8c via 0x5f72
- 0x629e EP3 receive gate: outer state==3 and callback set -> callback(buf,n),
  watchdog 2500; 0x62cc EP3 send gate: state==3 -> 0x475c
- 0x470a EP2 send -> 0x4338(driver=gp-0x194, ep=2, buf, len); returns zext.b
- 0x46d2 EP2 receive -> 0x3eb2(2, stack 0x40) -> 0x6150(gp-0x3e0, buf, n)

Inner transport (ctx+0x30), 10-state machine at 0x5f9e (jump table 0x6754,
states 1..10). Pending op at +0x84, next state at +0x80, retry counter +0x8c
(16-bit), last-sent seq +0x13/+0x15. State 1 runs pending op; state 2 backs
off 10 ticks then restores failed state (+0x3); state 3 resends buffer with
BE length at buf[2..3] every (ctx+0xc)/2 = 1000 ticks, max 798 sends, then
state 0; states 5-7 call session-object methods 0x6362/0x637a/0x6396
(session-open sequence); states 9-10 + flag bits 4/5/6 = session close;
default (0) pumps link events via 0x4fa8 on link object at +0x1c.

## iAP2 link packets (byte-exact from 0x542a)

Header (9 B): FF 5A | length BE16 (whole packet) | control | seq | ack |
session_id | checksum = -(sum of first 8 bytes) & 0xff.
Payload checksum = -(sum of payload bytes except itself).

LinkSyncRequest sent by 0x542a (23 B total):
- control = 0x80 (SYN) or 0xC0 (SYN|ACK) when flag bit10 set
- seq = last-sent+1, ack = last received in-seq psn (0 at first)
- payload (14 B) = 01 05 10 00 07 D0 01 F4 1E 03 0A 00 01 <cksum>
  = version 1, max_outgoing 5, max_len 0x1000=4096,
  retransmission_timeout 2000, ack_timeout 500, max_retransmissions 30,
  max_ack 3, sessions: [(id=10, type=0, ver=1)]
  (the init constants 798 and 2826 are these byte pairs packed as u16)
- resent every 1000 ms until answered (iAP2 negotiate; matches wiomoc 0.5 s)

Received-link parsing (0x5bec): requires count>8, FF 5A. control bits:
0x80 SYN (store their seq +0x12, ack +0x14 if 0x40; validate echoed sync
params against +0xc/+0xe/+0x10/+0x11/+0x16; set flags bit0; queue op 0x542a
via 0x4f68 to send SYN|ACK), 0x40 ACK (session-data path when payload
present), 0x20 EAK (pkt[0x14] -> +0x13), 0x10 RST (re-init 0x4fe6, resend
sync), 0x08 (flags |= 2). Data packets: session id byte must equal +0x16,
session type BE16 at payload dispatched: 0x1D00 -> queue {state 4, op 0x5bc0},
0x4A02 -> 0x5e9a, 0x1D02 -> 0x5e9e, 0xAE01 -> 0x5eb8, 0xEA04 -> 0x5ec6,
0x4A04 -> flags |= 4, 0x4A00 -> flags |= 2 (names per message catalogs:
0x4A00/0x4A02/0x4A04 Identify family, 0x1D00/0x1D02 RequestIdent family,
0xAE01 PowerUpdate, 0xEA00/0xEA01/0xEA04 EA session open/close/control).

## Complete session flow (decoded 2026-10-10, all byte-exact)

Message framing (control session, builders 0x53b0/0x53e4):
- Envelope 0x53b0(dst, max, type, body_len): `40 40 <len_be:2=body+6>
  <type_be:2>` then caller appends body; frame = body+7 bytes; last byte is
  -(sum) checksum (0x5502) which doubles as the zero trailer.
- Param entry 0x53e4(dst, max, id, src, len): `<entry_len_be:2=len+4>
  <id_be:2> <value>`. src=NULL means "value already at dst+4, header only".

Auth chip = MFi auth coprocessor on bit-banged I2C (NOT SPI):
- Bus: GPIOA PA13=SCL, PA14=SDA. CFGHR 0x40010804, BSRR 0x40010810,
  INDR 0x40010808. SDA drive = CFGHR pin14 MODE=01 (OR 0x01000000),
  release = CNF=01 MODE=00 (OR 0x04000000, mask 0xF0FFFFFF).
- START 0x4ff6, STOP 0x5048, ACK-read 0x5090 (sample INDR bit14, inverted),
  send-byte 0x50ec (MSB first), delay 0x4fec (n nop-iterations).
- Half-bit delay = global(gp-0x7e0)>>2; setup delay = >>1. Init 0x5350(50000).
- Chip address byte 0x22 write / 0x23 read (7-bit 0x11).
- Write txn 0x515c(addr, sel, buf, len): [START][0x22][sel][data][STOP],
  3 retries, 10000-delay between.
- Read txn 0x51cc(addr, sel, buf, len): [START][0x22][sel][STOP],
  [START][0x23][read n bytes, ACK all but last][STOP], 3 retries.
- Selectors: 0x10 write cmd byte, 0x21 write challenge, 0x11 read BE16
  response length, 0x12 read response bytes, 0x30 peek BE16 pending msg
  length, 0x31+cursor read 128-byte chunk of pending message.
- The chip stores the accessory identity; the outgoing 0x4A01 message is
  streamed FROM the chip in 128-byte chunks (pump 0x5584 via 0x6346/0x62ea).

Received-message dispatch (in 0x5bec, session types BE16 in data packets,
session id byte must equal ctx+0x16):
- 0x4A00 -> clear session cursor/count, queue bare ACK link packet (0x5bc0)
- 0x1D00 -> queue Identify reply op 0x5734
- 0x4A02 -> 0x5e9a: deliver challenge to session object, auth flow
- 0x1D02 -> 0x5e9e
- 0xAE01 -> 0x5eb8 (-> queue 0x5b62 StopPowerUpdates 0xAE02, empty body)
- 0x4A04 -> flags |= 4 (auth OK)
- 0xEA00 -> 0x5ec6
- 0xEA01 -> flags &= ~0x7FF (link reset)

Outgoing ops (state machine pending-op functions):
- 0x542a LinkSyncRequest (see above), 23 B
- 0x5584 stream pending chip message as 0x4A01: 128-byte chunks, KVP
  (id 0, entry_len = total+4, value = chunk); pulls next via 0x62ea/0x6346
- 0x569e auth reply 0x4A03: KVP(id 0, chip response from 0x63aa)
- 0x5734 Identify 0x1D01 (reply to 0x1D00), params:
  0 name 'Clicks Creator Keyboard' (24 incl NUL), 1 'CK-5200' (8),
  2 'Clicks Technology Limited' (26), 3 serial (runtime via 0x2276 getter,
  len = byte0-1, value at +1), 4 '1.2.2' (6), 5 '1.2.0' (6),
  6 `ea 02 ae 00 ae 02 ae 03` (8), 7 `ae 01` (2), 8 `02` (1),
  9 `00 64` = 100 (2), 10 49-B EA blob incl 'com.clickscompanion.protocol'
  (bytes 0x67f5..0x6825), 11 '79Q5CGN6JK' + `00 06 00 00 00 02` (17),
  12 'en\0' (3), 13 'en\0' (3), 14 21-B blob incl 'IAP2-X' (0x67e0..0x67f4),
  15 33-B blob incl 'HID-X' (0x6831..0x6851), 34 '020c3648d17f4624' (17).
  Param source bytes live in image 0x677c..0x6862.
- 0x5a6e EA announce 0xEA02: KVP(id 0, 'com.clickscompanion.app', 24)
- 0x5aea StartPowerUpdates 0xAE00 (KVP id 4, empty value)
- 0x5b62 StopPowerUpdates 0xAE02 (empty body)
- 0x5bc0 bare ACK link packet (9 B, no payload)

Auth relay state flow: challenge received (0x4A02) -> state 5: I2C write
challenge (0x6362/0x515c sel 0x21); state 6: write cmd byte 1 (0x637a sel
0x10); state 7: 0x63aa: read BE16 len (sel 0x11), read n bytes (sel 0x12)
-> op 0x569e replies 0x4A03.

Op ring: 0x4f68(link_obj, &req{u8 next_state, pad, ptr op}) enqueues into
8-slot ring (12 B per slot at link+0x4, cursors at +0x0/+0x1, wrap at 7);
0x4fa8(link_obj, &req) dequeues; default inner state 0 calls 0x4fa8 each
tick and adopts {next_state, op} into ctx+0x80/0x84.

Full session sequence:
attach (FF 55.., 475-tick retry) -> echo -> link sync 0x542a (1000-tick
retry, 798 max) -> SYN exchange, flags bit0/1 -> ACK -> NORMAL (bit3) ->
phone 0x1D00 -> Identify 0x1D01 -> phone 0x4A00 -> bare ACK + stream chip
identity as 0x4A01 chunks -> phone 0x4A02 challenge -> I2C auth -> 0x4A03 ->
phone 0x4A04 auth OK (flags bit2) -> 0xEA02 EA announce with
'com.clickscompanion.app' -> EA open (0xEA00/0xEA01) -> EP3 raw channel
active (outer state 3 + registered callback = existing 0x02 dispatcher).
Power updates: 0xAE01 received -> 0xAE02 stop reply.

## Stock keymap decode, 2026-10-10

The stock keymap lives INLINE in the HID builder at 0x2b1c..0x3010: each
matrix crossing is an individual bit test; the common shape is
`li alt; bnez a5 -> alt; li base` where a5 selects the SYM layer, and a
runtime "cursor mode" flag (ctx byte 0 bit 4) swaps some crossings to
arrow keys. Full table (crossing = matrix byte r, bit b; the custom
firmware uses the same scan indexing: PB row high, read PA0..PA5):

| r/b | base | SYM (a5) | cursor | notes |
| --- | ---- | ---- | ---- | ---- |
| 0/01 | q | 1 | | |
| 0/02 | u | 7 | | |
| 0/04 | k | ' | Down | |
| 0/08 | Backspace | | | |
| 0/10 | Enter | | | extra behavior keyed on layer byte 0x120 |
| 0/20 | | | | consumer bit 2 (rpt byte 6 |= 4) |
| 1/01 | w | 2 | Up | |
| 1/02 | i | 8 | Up | |
| 1/04 | d | ; | Right | |
| 1/08 | l | ' | Right | |
| 1/10 | v | / | | |
| 1/20 | (chord with 5/10) | | | consumer bit 1 when chorded |
| 2/01 | e | 3 | | |
| 2/02 | o | 9 | | |
| 2/04 | f | ; | | |
| 2/08 | (none) | | | spare crossing, no key |
| 2/10 | b | 1 | | |
| 2/20 | (mod byte |= 8) | | | Left GUI / Command |
| 3/01 | r | 4 | | |
| 3/02 | p | 0 | | |
| 3/04 | g | 9 | | |
| 3/08 | z | configurable (ctx 0x11e) | | shift interplay at 0x2df4 |
| 3/10 | n | , | | |
| 3/20 | Space | | | |
| 4/01 | t | 5 | | |
| 4/02 | a | - | Left | |
| 4/04 | h | 0 | | |
| 4/08 | x | 7 | | |
| 4/10 | m | . | | |
| 4/20 | (mod byte |= 1) | | | Left Ctrl |
| 5/01 | y | 6 | | |
| 5/02 | s | / | Down | |
| 5/04 | j | Clicks key (ctx 0x11f selects behavior) | Left | 0x2f7c config block |
| 5/08 | c | 2 | | |
| 5/10 | (sticky companion) | | | one-shot SYM mechanics at 0x2876 |
| 5/20 | | | | consumer bit 0 (rpt byte 6 |= 1) |

The three consumer bits map to the stock HID descriptor usages
0x01AE / 0x029D / 0x00CF [UNVERIFIED semantics]; the descriptor carries
three 1-bit fields. The custom firmware instead exposes one 16-bit
consumer field and maps one crossing to Eject (0x00B8), the usage that
toggles the iOS on-screen keyboard on 9981-style keyboards.

## Stock backlight decode, 2026-10-10

Correction of the earlier "SPI1" note (arithmetic error): the base is
0x40012C00 = TIM1, not SPI1 (0x40013000).

- Three PWM channels, TIM1 CH1/2/3 on PA8/PA9/PA10 (stock 0x306a:
  GPIO mask 0x800, AF push-pull).
- Timebase: PSC = 0, ARR = 6000 (stock 0x2300: a2 = 0x1770).
- Brightness (stock 0x3010): duty = (0x100 - raw) * 6000 >> 8 written to
  CCR1/CCR2/CCR3; the raw config byte is inverted (0 = brightest).
- Channel gates (stock 0x3050 -> 0x4ef8): CCER bits CC1E/CC2E/CC3E.
- Init: CCMR1/CCMR2 PWM mode 1 + preload, BDTR MOE (0x4eba), CR1 ARPE
  (0x4ed0) and CEN (0x4ea2). Boot reads the persisted raw byte from
  config offset 0x165-equivalent, then stock DISABLES all three
  channels (main loop 0x23dc..0x23fc) pending its activity state
  machine, which was not fully decoded.
- Custom firmware ports all of the above but keeps the channels enabled
  at boot (deliberate deviation: the stock activity-based on/off state
  machine is future work).

## Remaining minor unknowns

- Dispatch tails 0x5e9a/0x5e9e/0x5eb8/0x5ec6 delivery details into the
  session object (obj+0x8 len, obj+0x9 data).
- 0x62ea peek body, 0x2276 serial getter source, gp-0x7e0 timing global
  init, 0x4f56 ring space check.
- Stock backlight activity-based on/off state machine (who re-enables
  the three channels after boot).
- Consumer usages 0x01AE / 0x029D / 0x00CF semantics (three descriptor
  bits); the SYM-value semantics of the second/third-row crossings
  ([UNVERIFIED] whether a5 means the SYM layer for all of them).
- SPI1 (0x40013000) accesses near 0x3024/0x4ca2: likely NOT auth; park.

## Doc corrections pending

USB_AND_IPHONE.md contains a wrong [CORRECTION]: the attach constant
ff 55 02 00 ee 10 IS in the image at file offset 0x48d0 (linked 0x68d0).
The earlier "correction" confused file offset 0x28d0 (code) with 0x48d0.
Also the reject template lives at file 0x44ec (linked 0x64ec) as documented.

## User requirements (2026-10-09)

- MFi compliant from the first flash; port from stock images only; no case
  opening; no dev board unless user says so.
- Layout/layers/manageability model: follow ZitaoTech/9981_BLE_USB_Keyboard_Pro
  (ZMK-style): 4 layers (QWERTY / SYM+num via sticky/momentary "SYM" /
  function+BT via "aA" / reserved), sticky ctrl, to-layer on double-tap,
  live keymap editing without reflash (ZMK Studio equivalent -> our
  Inspector keymap editor over the EA session), Eject keycode to toggle the
  iOS on-screen keyboard.
