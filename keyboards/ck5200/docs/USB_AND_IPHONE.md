# USB and iPhone compatibility

The stock configuration has three USB interfaces:

1. HID boot keyboard, interrupt IN `0x81`, OUT `0x01`.
2. Vendor `FF/F0`, bulk IN `0x82`, OUT `0x02`. This is the Android firmware updater path.
3. Vendor `FF/F0` with alternate setting and bulk IN `0x83`, OUT `0x03`.

The stock firmware also contains iAP2/Clicks companion identity strings. A plain QMK HID implementation does not reproduce that Apple accessory path.

For that reason the first QMK image is a bring-up target for a PC/Mac USB host. iPhone typing is [UNVERIFIED] until the Apple-facing path is understood or tested. The regular-USB updater must remain present in custom firmware so subsequent builds can still be staged without opening the case.
