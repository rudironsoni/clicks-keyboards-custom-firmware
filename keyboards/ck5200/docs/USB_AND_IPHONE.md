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

Every item below is **NOT VERIFIED** on custom firmware:

1. Cold boot and enumeration when attached to the iPhone 15 Pro Max.
2. Physical key mapping, key down/up, rollover, Shift/Control/Option/Command, and stock special-key behavior.
3. Intended base layout and layers, with no stuck keys or missed releases.
4. Lock, unlock, sleep, wake, disconnect, and reconnect.
5. Backlight brightness and timeout behavior.
6. Charging through the case while typing, plus USB behavior with the charger attached or removed.
7. Return to the exact stock application and normal phone operation.

The current image has no Apple accessory session, no backlight driver, and only a diagnostic matrix keymap. Stock includes `IAP2-X`, `com.clickscompanion.protocol`, and `com.clickscompanion.app`. Plain HID support on this case must be tested rather than assumed. Companion-app control requires its own verified protocol if preserving it is necessary. Do not claim iOS compatibility from Android analysis or Mac enumeration.
