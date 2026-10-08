# Build and inspect the CK-5200 image

Custom installation is blocked until the stock transport and independent recovery are verified. These instructions build and inspect files; they do not establish iOS compatibility.

## Existing tools

macOS needs CMake, Ninja, Python, and libusb. The setup script downloads a checksum-verified RISC-V toolchain and pins QMK, TinyUSB, and the WCH SDK. Run from the repository root:

```sh
bash keyboards/ck5200/scripts/setup.sh
bash keyboards/ck5200/scripts/test.sh
bash keyboards/ck5200/scripts/build.sh
```

Source dependencies are in `keyboards/ck5200/external`. They are nested Git checkouts managed by `scripts/bootstrap.sh`, not tracked submodules. Do not modify or repin them as a build workaround. The build output is in `keyboards/ck5200/build`, not in the source tree.

## Image contract

- Application start: `0x00002000`.
- Maximum application size: `0x6A00` bytes.
- Application end must not cross `0x00008A00`, the start of staging through the flash alias.
- Configured RAM: `0x20000000..0x20005000`, including a 2048-byte reserved stack.
- Reset and interrupt targets must be within executable application code.

The binary validator checks reset/vector information and image extent. Its `--elf` option also checks the linked artifact. Use both for custom builds:

```sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/validate_image.py \
  keyboards/ck5200/build/ck5200_qmk.bin \
  --elf keyboards/ck5200/build/ck5200_qmk.elf
bash keyboards/ck5200/scripts/flash-qmk.sh
```

The old zero-based image must fail validation. The stock application can be checked without an ELF, but that is a narrower check and is labeled as such.

## USB reads

```sh
keyboards/ck5200/.venv/bin/python keyboards/ck5200/tools/ck5200_usb.py inspect
```

This does not send SET_CONFIGURATION, detach drivers, or claim an interface. It establishes descriptor presence only.

The optional `inspect --query-version` command sends `02 03`, a read-only stock version request. It currently fails against the connected iPhone firmware with `ff 55 02 00 ee 10`. The tool records the bytes and stops. See [USB_AND_IPHONE.md](USB_AND_IPHONE.md).

## Installation and restoration

`install.sh` refuses installation. `flash-qmk.sh` remains offline even if `FLASH=YES` is set. The direct Python updater refuses stock-device update commands because the stock iPhone session is unsupported. Do not remove this check merely because the image builds.

The existing Python update implementation remains available for the experimental custom updater only. Its status, command, offset, and commit-size checks remain mandatory. Neither a descriptor version nor an explicit confirmation proves that recovery works.

Use `scripts/revert-stock.sh --download-only` to preserve the stock application. Actual transfer, power-loss behavior, boot acceptance, and stock restoration remain unverified. See [RECOVERY.md](RECOVERY.md).
