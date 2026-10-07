# Clicks keyboards custom firmware

I really like the Clicks Keyboard Case. This is a hobby project to see how far I can take it by running custom QMK firmware on it.

The goal is simple: keep the keyboard I already enjoy using, but add the kind of features and customization that QMK makes possible.

That means things like custom keymaps, layers, shortcuts, macros, and anything else that makes sense on such a tiny physical keyboard.

I am starting with the CK-5200 because that is the keyboard I have in hand. The repo is intentionally organized so other Clicks keyboard models can be added later.

## Keyboards

| Model | Status |
| --- | --- |
| [CK-5200](keyboards/ck5200/) | Work in progress |

## What is in here

The CK-5200 work currently includes:

- a QMK firmware port in progress
- the keyboard matrix mapping work
- CH32V20x startup and USB code
- a USB firmware update tool
- build and validation scripts
- notes about what I have confirmed so far
- GitHub Actions builds

The interesting part is that the stock keyboard already has a USB firmware update path. I am trying to understand that path well enough to use it for my own firmware, without having to open the keyboard every time I want to test a build.

## Current state

The project is still experimental.

The build system and USB update path are both being worked on. A compiled file is not automatically a safe file to flash, so each step is being tested separately before I trust it on the keyboard.

For the latest state, see [keyboards/ck5200/STATUS.md](keyboards/ck5200/STATUS.md).

## Repo layout

```text
keyboards/
  ck5200/
    firmware/
    scripts/
    tools/
    tests/
    docs/
```

Each keyboard model gets its own folder. Shared code can come later if another model actually needs it.

## A small warning

This is hobby firmware for hardware I own and am happy to experiment with.

If you try any of this on your own keyboard, assume that a bad firmware image can leave the device unusable until you find another recovery method.
