# Flashing the CK-5200: the definitive guide

This document is the single source of truth for flashing the custom
firmware onto a Clicks CK-5200 keyboard case. It covers what is in the
image, what the Apple session stack does, the risks, the procedure,
and how to get back to stock. Read it fully before flashing.

**The one-sentence risk statement**: if the flash succeeds, you have a
typing, backlit, MFi-capable keyboard with four editable layers. If it
fails, you likely have a keyboard that still types over USB but cannot
talk to the phone app; the Mac path restores stock in about two minutes.
The only unrecoverable case is USB itself dying, which the offline
evidence makes unlikely.

---

## 1. What is in the image

The custom firmware image is 23,392 bytes of ARM-free RISC-V machine
code for the CH32V203 microcontroller. It occupies the application
region `0x08002000..0x08008A00` of the 64 KB flash. The stock boot
region (`0x08000000..0x08002000`) and the staging area
(`0x08008A00..0x08008A04 + image`) are never touched by the image
itself.

### Memory map

| Region | Address range | Size | Owner |
| --- | --- | --- | --- |
| Boot / installer | `0x08000000..0x08002000` | 8 KB | Stock, read-only from our side |
| **Application** | `0x08002000..0x08008A00` | 27 KB max | **Our image lives here** |
| Staging length | `0x08008A00..0x08008A04` | 4 B | A3 writes the committed size |
| Staging image | `0x08008A04..0x0800F400` | 27 KB max | A2 writes the staged bytes |
| Keymap EEPROM | `0x0800F800..0x0800FC00` | 1 KB | Wear-leveled dynamic keymap |
| Free | `0x0800FC00..0x08010000` | 1 KB | Unused |

### Firmware composition (in the 23,392 bytes)

| Module | Approx. size | What it does |
| --- | --- | --- |
| TinyUSB device core | 4,470 B | USB enumeration, control transfers, endpoint management |
| QMK keyboard core | 3,100 B | Matrix scanning, keycode processing, layers, one-shot, report building |
| iAP2 session stack | 1,079 B | Apple accessory link layer: attach, sync, identify, session open |
| iAP2 auth driver | 1,033 B | Bit-banged I2C on PA13/PA14 to the MFi authentication chip |
| Dynamic keymap | 559 B | Four-layer 6x6 keymap over wear-leveled EEPROM |
| Backlight driver | 200 B | TIM1 PWM on PA8/PA9/PA10, stock duty formula |
| Consumer keys | 120 B | 16-bit consumer usage in the HID report (Eject for iOS) |
| Update protocol | 515 B | A1/A2/A3/A0 staging and the `0x03`/`0x90-0x93` dispatcher |
| Staging writer | 143 B | 256-byte page writes to the staging area |
| WCH SDK (trimmed) | 1,100 B | Flash, GPIO, RCC, timer drivers |
| Descriptors + strings | 570 B | USB device/config/HID descriptors, iAP2 strings |
| Keymap data + misc | ~10 KB | QMK tables, HID report, linker overhead, ident parameters |

---

## 2. What the Apple session stack does (MFi)

When you attach the Clicks case to an iPhone, iOS does not simply
"see a USB keyboard." It expects the accessory to perform an Apple
proprietary handshake before it will open an External Accessory (EA)
session. Without this handshake, the official Clicks app and your
inspector app cannot reach the keyboard at all. The phone still
accepts plain HID typing, but no app-level communication works.

The handshake has four stages, all decoded from the stock V122 image
and ported byte-faithfully (full decode record: [STOCK_DECODE.md](STOCK_DECODE.md)):

### Stage 1: Attach

The keyboard sends `FF 55 02 00 EE 10` on its bulk endpoint every 475
milliseconds. When the iPhone responds with the same six bytes, the
link layer starts. If no iPhone responds (e.g., plugged into a Mac),
the keyboard keeps sending and the Mac sees the pre-session reject
packet on any other traffic.

### Stage 2: Link sync

The keyboard sends a 23-byte LinkSyncRequest:

```
FF 5A 00 17 80 <seq> <ack> 00 <ck> | 01 05 10 00 07 D0 01 F4 1E 03 0A 00 01 <ck>
```

This declares: version 1, max 5 packets in flight, 4096-byte packets,
2000 ms retransmission timeout, 500 ms ack timeout, 30 retransmissions,
ack every 3 packets, control session id 10. The iPhone replies with
its own sync parameters. Both sides acknowledge. If the parameters do
not match, the keyboard restates its own as SYN|ACK.

### Stage 3: Identify and authenticate

The iPhone sends `0x1D00` (RequestIdentify). The keyboard replies with
`0x1D01` carrying sixteen parameters: the keyboard's name ("Clicks
Creator Keyboard"), model ("CK-5200"), manufacturer, serial number,
firmware and hardware versions, the EA protocol string
("com.clickscompanion.protocol"), and a token.

Then the iPhone sends `0xAA00` (RequestCertificate). The keyboard
relays this over bit-banged I2C to the MFi authentication chip on the
board (a licensed Apple chip on PA13/PA14). The chip streams its
Apple-signed certificate back in 128-byte chunks as `0xAA01` messages.

Then the iPhone sends `0xAA02` (Challenge). The keyboard relays the
challenge bytes to the auth chip over I2C (selector `0x21`), sends a
"go" command (selector `0x10`), polls the chip's status until it
signals completion, reads the response length (selector `0x11`) and
the response bytes (selector `0x12`), and replies to the iPhone with
`0xAA03`.

The iPhone validates the certificate chain and the challenge response.
If both pass, it sends `0xAA04` (AuthAccepted). The keyboard then sends
`0x1D02` (SessionOpen), and the EA session is live.

### Stage 4: EA data channel

Once the session is open, the raw companion protocol (the `0x02`
dispatcher: version, keymap, flash, restore) flows on the third USB
endpoint pair (`0x03`/`0x83`). The phone app can now read the keymap,
flash images, and restore stock.

### What happens if the session stack has a bug

If any stage fails, the phone app cannot reach the keyboard. Plain HID
typing still works (the USB HID interface is independent). The Mac
path still works (the EP2 dispatcher fallback responds on any host that
is not an iPhone doing the attach handshake). The restore procedure is
identical: connect to a Mac, run the restore, reboot.

---

## 3. Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| MFi session stack has a runtime bug | Medium (static-only verification) | Phone app cannot reach the keyboard; typing still works; Mac restore works | Run the self-test after flashing; if it fails, restore stock from the Mac |
| Auth chip address is `0x20`, not `0x22` | Low (the driver tries both after 20 failed peeks) | Auth challenge fails; session never opens | The driver auto-toggles; if the chip needs a third address, the session fails but recovery is unchanged |
| I2C timing too fast or too slow | Low (derived from the same clock as stock) | Auth chip does not respond; session fails | Recovery unchanged |
| HID report descriptor rejected by iPhone | Low (standard 6KRO boot layout plus 16-bit consumer; the iPhone's HID parser is broad) | Keyboard does not type on the phone | Mac restore; the descriptor can be adjusted |
| USB enumeration fails | Very low (TinyUSB core, stock descriptors) | Keyboard invisible to all hosts | Only recoverable by physical flash programming (see [RECOVERY.md](RECOVERY.md)) |
| Backlight does not light | Low (TIM1 register writes decoded from stock) | Keyboard dark but functional | Cosmetic; fixable in a re-flash |
| Flash write corrupted the image | Very low (the staging writer verifies every page) | Keyboard does not boot; USB dead | The staging area is outside the application region; the boot region's installer can retry from a fresh A1/A2/A3 cycle if USB still enumerates |
| Keymap EEPROM corrupted | Very low (wear-leveled, four pages) | Keys type wrong characters | `0x93` reset command restores the factory layout over the session or the Mac path |

**What we cannot eliminate**: the session stack and the auth relay have
never run on real hardware. The decode is instruction-by-instruction
from the stock image, the link layer matches the public wiomoc/iap2
implementation exactly, and the harnesses execute the real code against
a scripted phone and a waveform-level I2C slave. But no static work
can prove the auth chip responds or the iPhone accepts the session.
The first flash is the first live test.

**What we can guarantee**: the boot region is never touched. If our
image is broken, the stock installer below `0x2000` is intact. If USB
still enumerates, the A1/A2/A3 update dialect can restore stock. The
only way to permanently lose the keyboard is a USB-level failure that
prevents all host communication, which requires either a corrupted
boot region (impossible from our image) or a hardware fault.

---

## 4. Before you flash: the preflight check

Run the preflight script. It validates everything that can be checked
without hardware:

```sh
bash keyboards/ck5200/tools/preflight_check.sh
```

This checks:

1. Both stock images are present and match the recorded hashes.
2. The custom firmware image passes full ELF/BIN validation.
3. The image is within the 27 KB application limit.
4. The restore-cycle harness passes: the full stock image and the
   full custom image round-trip through the real update handler and
   the real staging writer, byte-for-byte.
5. All host harnesses pass: session replay (13 scenarios), I2C
   waveform (4 scenarios), keymap layout, backlight registers,
   consumer keys, update protocol, stock reads.
6. Every commit on main builds standalone (11/11 verified).
7. The iOS app's Swift protocol checks pass.
8. The bundled app images match the built firmware.

If any check fails, do not flash. Fix the failure first.

### Manual preflight on the connected keyboard

Before the first flash, on the **stock** firmware, verify:

1. The keyboard types on the iPhone (open Notes, type).
2. The Clicks Inspector app discovers the accessory and opens a session.
3. The version read returns stock `01 20 01 22`.
4. At least one settings read (e.g., brightness `02 84`) returns a value.

This establishes that the keyboard is in its normal state before you
change anything. Record the results.

---

## 5. The flash procedure

### From the iPhone (recommended first flash)

**Prerequisite**: stock firmware is running on the keyboard, and the
Clicks Inspector app is installed and has verified a session (the
preflight above).

1. Open the Clicks Inspector app. Verify it identifies the firmware as
   "Stock firmware detected."
2. Tap **Flash bundled custom firmware** (23,392 bytes).
3. Confirm. The app stages the image through A1/A2/A3 (the same
   protocol the official Clicks app uses for updates).
4. The keyboard reboots to install. **Do not detach the case.** Wait
   for it to finish (about 10 seconds; the backlight will cycle).
5. After the reboot, the keyboard re-enumerates. Tap **Refresh** in
   the app.
6. Open a new session (the keyboard now runs the custom firmware; the
   MFi stack handles the handshake).
7. The app should identify "Custom QMK firmware detected" and load
   the keymap.
8. Tap **Run self-test**. All checks should pass.

### From a Mac (recovery and updates)

```sh
# The Mac tool routes on bcdDevice 0x9001 (custom firmware marker).
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
```

If the keyboard reports `bcdDevice 0x9001`, the Mac tool can flash:

```sh
# Stage and install the custom firmware.
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py flash \
  keyboards/ck5200/build/ck5200_qmk.bin

# Or restore stock.
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py restore-stock
```

---

## 6. After you flash: the validation checklist

### What to check, in order

| Step | What to check | How to check | What it proves |
| --- | --- | --- | --- |
| 1 | USB enumeration | Plug into a Mac, run `inspect` | The device is alive; `bcdDevice 0x9001` identifies custom firmware |
| 2 | HID keyboard | Open any text field, type all 26 letters, digits, Enter, Space, Backspace | The matrix scan, keycode mapping, and HID report all work |
| 3 | Backlight | The keyboard lights up at the persisted brightness | The TIM1 PWM driver works |
| 4 | MFi session | Open the Clicks Inspector app, tap Refresh, open a session | The Apple handshake, auth relay, and session stack all work |
| 5 | Identify | The app shows "Custom QMK firmware detected" | The version dispatcher answers `02 03` with "QM" |
| 6 | Self-test | Tap "Run self-test"; all checks should pass | The full keymap matches the decoded factory layout; the write path round-trips |
| 7 | Key test | Tap the key test field, type on the physical keyboard | Characters appear; HID report path works end to end |
| 8 | Layout preview | Browse all four layers in the layout preview | The decoded QWERTY, SYM, cursor, and reserved layers display correctly |
| 9 | Restore path | From the app, tap "Restore stock firmware" | The A1/A2/A3 update dialect still works for restore |
| 10 | Re-flash custom | After restore, flash custom again | The cycle is repeatable |

### What to do if a check fails

| Failed check | Likely cause | What to do |
| --- | --- | --- |
| Step 1 (USB) | Boot failure or USB stack crash | The keyboard may need physical flash programming; see [RECOVERY.md](RECOVERY.md) |
| Step 2 (HID) | Keymap or matrix bug | Run the keymap reset (`0x93`); if typing still fails, restore stock |
| Step 3 (backlight) | TIM1 or GPIO bug | Cosmetic; re-flash with a fix, or ignore |
| Step 4 (MFi session) | Session stack, auth chip, or I2C bug | Restore stock from the Mac; report the failure |
| Step 5 (identify) | Dispatcher or USB vendor bug | Restore stock from the Mac |
| Step 6 (self-test) | Keymap corruption or protocol bug | Run the keymap reset; if the self-test still fails, restore stock |
| Step 7 (key test) | HID report or consumer field bug | Check if plain keys work without the consumer field; if not, restore stock |
| Step 8 (layout) | Keymap mismatch | The keymap editor can fix individual keys; the self-test pinpoints which ones |

---

## 7. How to revert to stock

There are three ways, strongest first. All use the same A1/A2/A3/A0
update dialect that the stock firmware itself uses for updates, so
they are the same protocol, not a custom recovery mode.

### From the phone (if the session stack works)

1. Open the Clicks Inspector app.
2. Tap **Restore stock firmware (V122)**.
3. Confirm. The keyboard reboots into the stock installer.
4. Wait about 10 seconds. The keyboard re-enumerates as stock.
5. Open a session and verify the version read returns `01 20 01 22`.

### From a Mac (if the phone session is broken but USB works)

```sh
# The Mac tool sends the same A1/A2/A3/A0 dialect on the EP2 bulk
# endpoint. It routes on bcdDevice 0x9001, so it cannot accidentally
# flash a stock keyboard that is not expecting an update.
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py restore-stock
```

### From a second phone (if the first phone is unavailable)

Install the Clicks Inspector app on any iPhone, connect the keyboard,
and use the restore button. The update dialect is identical regardless
of which host drives it.

### What does NOT lose the keyboard

- A failed flash (the staging area is outside the application region).
- A corrupted image (the staging writer verifies every page).
- A power loss during A2 (the staging area holds the partial image;
  the boot region is untouched; a fresh A1/A2/A3 cycle rewrites it).
- A session stack bug (the Mac path is completely independent).
- A keymap corruption (the `0x93` reset restores the factory layout).

### The only unrecoverable case

If USB enumeration itself fails (the device does not appear to any
host), the boot region may be damaged or the USB peripheral may be
wedged. This requires physical flash programming; see the hardware
backup and restore guide in [RECOVERY.md](RECOVERY.md#hardware-backup-and-restore-guide-2026-08-10).
No normal flash operation can cause this: our image never writes
below `0x2000`.

---

## 8. Firmware composition details

### USB topology (identical to stock)

The custom firmware presents the same three USB interfaces as stock:

- Interface 2: HID boot keyboard, interrupt endpoints `0x81`/`0x01`,
  64-byte packets, 1 ms interval. The iPhone sees this as a standard
  external keyboard.
- Interface 0: vendor `FF/F0/00`, bulk endpoints `0x82`/`0x02`. This
  carries the iAP2 link layer (the MFi handshake) and, when no session
  is active, the dispatcher fallback for the Mac path.
- Interface 1: vendor `FF/F0/01`, bulk endpoints `0x83`/`0x03`, present
  only at alternate setting 1. The iPhone selects this when the EA
  session opens; the raw companion protocol flows here.

The USB strings match stock: "iAP2 Interface" (index 4),
"com.clickscompanion.protocol" (index 5), "Clicks Technology Limited",
"Clicks Creator Keyboard", serial "190200001".

The HID report descriptor is 45 bytes: a 6KRO boot keyboard report
(modifiers + reserved + 6 keys) followed by a 16-bit consumer usage
field, for a 10-byte total report. Stock used 4KRO plus three 1-bit
consumer fields; the custom firmware keeps six-key rollover and adds
the 16-bit consumer field for the Eject key (iOS on-screen keyboard
toggle).

### Deliberate differences from stock

1. `bcdDevice` is `0x9001` (not `0x0122`), so the Mac flash tool can
   distinguish custom from stock and refuse to flash stock by mistake.
2. The HID report descriptor is 6KRO (not 4KRO) plus a 16-bit consumer
   field (not three 1-bit fields).
3. The keymap layer 1 (SYM) and layer 2 (cursor) values are the stock
   values placed on QMK-native momentary/one-shot layers instead of
   the stock's runtime mode toggles.
4. The backlight channels stay enabled at boot (stock disables them
   pending an activity state machine that was not decoded).
5. Commands `0x86`/`0x88`/`0x8a`/`0x8c`/`0x8d` (backlight delay, idle,
   settings) are not implemented; they return the unsupported-command
   status rather than lying with canned replies.

---

## 9. Pointers

- Full decode record with every stock address: [STOCK_DECODE.md](STOCK_DECODE.md)
- Recovery ladder and hardware backup: [RECOVERY.md](RECOVERY.md)
- Build and inspect instructions: [BUILD_AND_FLASH.md](BUILD_AND_FLASH.md)
- iOS app usage and capabilities: [ios/ClicksInspector/README.md](../../ios/ClicksInspector/README.md)
- Overall project status: [STATUS.md](../STATUS.md)
