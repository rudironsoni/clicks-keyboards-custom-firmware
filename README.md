# Clicks keyboards custom firmware

I love the Clicks Keyboard Case and wanted to see how far I could take it with QMK.

This is a hobby project. The goal is to keep the hardware that already works really well and add the things I miss from a programmable keyboard: custom layouts, layers, shortcuts, macros, and whatever else turns out to be useful on a tiny phone keyboard.

The CK-5200 is the first keyboard I am working on. Other Clicks models can live next to it later.

## CK-5200

Everything for the CK-5200 is in [keyboards/ck5200](keyboards/ck5200/).

If you have that model, start there. The README in that folder has the actual setup, build, install, and restore commands.

Current state: the port is still experimental. CI builds the firmware and runs the protocol tests, but a successful build does not mean I have proven every failure mode on real hardware.

## Repo layout

```text
keyboards/
  ck5200/
    firmware/   QMK + CH32V20x code
    scripts/    setup, build, install, restore
    tools/      USB updater and image tools
    tests/
    docs/
```

Each keyboard gets its own folder until there is something genuinely useful to share between models.
