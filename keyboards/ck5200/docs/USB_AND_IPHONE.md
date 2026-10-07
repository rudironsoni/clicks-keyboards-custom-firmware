# USB and iPhone notes

The stock CK-5200 exposes three USB interfaces.

1. A normal HID keyboard interface on `0x81` and `0x01`.
2. A vendor interface on `0x82` and `0x02`. The Android app uses this one for firmware updates.
3. Another vendor interface on `0x83` and `0x03`.

The stock firmware also contains Apple accessory and Clicks companion strings.

That matters because a plain QMK keyboard only gives us normal USB HID. It does not automatically reproduce the extra Apple-specific behavior from the original firmware.

So the first QMK milestone is simple:

- make the MCU boot
- make USB enumerate
- make the keyboard scan correctly
- make keys type on a Mac or PC
- keep the regular USB firmware-update path working

After that, I can plug it into the iPhone and see what actually happens.

iPhone support is still [UNVERIFIED].
