# CK-5200 status

## Builds now

Yes. The current source builds successfully in GitHub Actions.

Current test firmware:

```text
file     ck5200_qmk.bin
size     18,940 bytes
hex      0x49fc
limit    0x6a00
sha256   f23d782b15b3b445d540243306cd404a4bc0ebd17f5df4084998cbf6a3053c3d
```

Flash use is 69.8 percent of the limit used by the stock updater.

The CI job also builds the ELF, HEX, map file and a full disassembly.

## Tests that pass

- Python tests for the USB update packets
- C test for the firmware-update command parser
- full RISC-V cross-build
- image-size check
- reset-instruction check
- ELF inspection in CI

## What is in the firmware

- QMK core
- 6x6 CK-5200 matrix scanner
- TinyUSB keyboard HID
- the Clicks firmware-update USB interface on `0x02` and `0x82`
- A1, A2, A3 and A0 update commands
- the same staging address used by the stock firmware
- a diagnostic keymap where every matrix position types a different character

## What I have not proven on the physical keyboard yet

The first custom flash is still the big test.

The USB transfer format is understood and the image now builds cleanly, but I have not yet proven that the code which runs after reboot accepts this custom image and installs it correctly.

I also have not proven that the iPhone will accept the QMK USB device. The original firmware has extra Apple accessory handling.

## Build, install and restore

From `keyboards/ck5200`:

```sh
bash scripts/setup.sh
bash scripts/build.sh
bash scripts/install.sh
```

To go back to official Clicks firmware:

```sh
bash scripts/revert-stock.sh
```

The USB restore only works while the keyboard can still expose its firmware-update interface. If the custom firmware prevents USB from starting, recovery needs a hardware programmer/debug connection.

See [README.md](README.md) for the full commands and [docs/RECOVERY.md](docs/RECOVERY.md) for the restore details.
