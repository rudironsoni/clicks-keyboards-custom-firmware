# Going back to the stock CK-5200 firmware

There are two different recovery cases.

## The keyboard still appears over USB

This is the easy case.

From `keyboards/ck5200`, run:

```sh
bash scripts/revert-stock.sh
```

The script:

1. downloads the official CK-5200 1.2.2 firmware
2. checks its SHA-256 before doing anything
3. asks you to type `STOCK-CK-5200`
4. sends the stock image through the normal firmware-update USB interface
5. reboots the keyboard

The expected image is:

```text
iKeyboard_CK-5200_V122_120.bin
SHA-256 8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8
```

You can check whether the updater is reachable first:

```sh
.venv/bin/python tools/ck5200_usb.py inspect
```

If that command finds `352e:2306` and endpoints `0x02` and `0x82`, the regular USB update path is available.

## The keyboard no longer appears over USB

The regular USB restore cannot help in this case because there is nothing for the host tool to talk to.

Do not keep retrying the USB script.

Recovery then needs direct access to the MCU through its programming/debug interface. The exact CK-5200 pad layout and a tested hardware recovery procedure are not documented yet, so I am not claiming that path is ready.

That is also why the first custom flash should be treated as an experiment, not as a normal QMK update.
