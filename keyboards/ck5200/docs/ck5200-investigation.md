Investigation baseline, 2026-10-08, source revision fd502d569391d61dc95d30d243298c5836937070. This records the pre-repair state, including then-current line numbers and temporary artifact paths. See [STATUS.md](../STATUS.md) for current verification. The stock application and APK are preserved in the ignored .stock directory; addresses and captured bytes are recorded in HARDWARE.md and USB_AND_IPHONE.md. Temporary build and disassembly files are not durable evidence.

**→ Verdict**

Rudi, do not flash the current image. This is a useful QMK bring-up project, but it is not a working replacement firmware for your iPhone 15 Pro Max case.

Issue: The custom image uses a different application address from the stock image, the host updater does not handle the traffic observed on the real keyboard, and recovery is unproven.
Cause: The build and tests check compilation and selected packet formats. They do not check the stock boot contract or the real USB session.
Fix: Keep stock firmware on the keyboard. Establish the update transport and recovery path, then correct the image address before any custom flash.

This investigation changed no tracked project files. It sent no update, erase, reset, or firmware-write commands. Device checks read USB descriptors and one pending bulk IN packet. Build and analysis artifacts are in `/tmp`.

**→ What failed in your attempt**

Your terminal log ends with `error: short response: expected >=8 bytes, got 6`.

The failure occurs in `Updater.begin()`. It sends A1, reads one USB packet, then immediately parses it as an A1 reply. Firmware chunks, A3 commit, and A0 reboot occur only after that method returns. [INFERENCE] Your logged attempt sent no firmware chunks and did not request commit or reboot. This is not a complete flash-memory readback, so it does not prove the contents of every flash region.

Sources: [A1 and response parsing](../../../keyboards/ck5200/tools/ck5200_usb.py:160), [flash sequence](../../../keyboards/ck5200/tools/ck5200_usb.py:245).

I independently read this packet from endpoint `0x82`, without sending any OUT command:

```text
unsolicited_bulk_in_length= 6
unsolicited_bulk_in_hex= ff 55 02 00 ee 10
```

The same six bytes appear at file offset `0x48d0` in the verified stock image. Published iAP2 research identifies that byte sequence as an Apple accessory handshake. Source: [USENIX research, handshake section](https://www.usenix.org/system/files/vehiclesec25-won.pdf).

[INFERENCE] Your tool probably read this unrelated handshake as the A1 response. Your original error did not record the bytes, so I cannot verify that the earlier packet was identical. I fed the packet read in this session into the existing updater through an offline fake transport and reproduced the exact error.

The stock binary's A1 handler constructs an eight-byte reply, including `08 02 a1`, a status byte, and `00 00 6a 00`. Thus, changing the expected reply length from eight to six is not justified. The missing work is to establish the session, framing, mode, and response routing used by the official updater. The current code assumes that the next bulk packet is the reply. It also loses the raw bytes when the reply is too short. [Host transport](../../../keyboards/ck5200/tools/ck5200_usb.py:142), [parser](../../../keyboards/ck5200/tools/ck5200_usb.py:71).

**→ Critical finding: wrong application address**

The custom linker places the application at `0x00000000`. The stock image contains a reset-vector entry of `0x00002000`, and its startup address calculations resolve correctly when the image is based at `0x00002000`. [Custom linker](../../../keyboards/ck5200/firmware/platform/ch32v20x/linker.ld:10).

I decoded the actual AUIPC/ADDI instruction pairs from both binaries. These instructions calculate addresses relative to the current instruction. The checked output was:

```text
stock load_base=0x0:
  gp=0x1fffe840 sp=0x20003000 data_destination=0x1fffe000
stock load_base=0x2000:
  gp=0x20000840 sp=0x20005000 data_destination=0x20000000
custom load_base=0x0:
  gp=0x20000878 sp=0x20005000 data_destination=0x20000000
custom load_base=0x2000:
  gp=0x20002878 sp=0x20007000 data_destination=0x20002000
stock reset vector stored= 0x2000
custom reset vector stored= 0x0
```

Here `sp` is the stack pointer, and `gp` is a pointer used to access program data. If the current custom image runs at `0x2000`, its stack pointer becomes `0x20007000`, beyond the `0x20005000` end of RAM assumed by this build. Its data initialization and interrupt addresses also no longer match their intended locations.

The stock address model has another consistent boundary: `0x2000 + 0x6A00 = 0x8A00`, exactly where the staging area begins in the stock code.

[INFERENCE] The first 8 KiB holds a separate boot/install component, and the normal updater installs the application after it. I cannot verify the boot component itself because the downloaded file contains the application, not a full device dump. This uncertainty does not remove the verified mismatch between the stock and custom images.

Fix: establish the complete memory map, link for the verified application address, and make image validation check that address, vector targets, RAM targets, and flash-region boundaries. A successful size check and a JAL opcode do not establish these facts. [Current validator](../../../keyboards/ck5200/tools/validate_image.py:9).

Binary evidence: [stock startup disassembly](/tmp/ck5200-investigation-stock-linked.dis:3603), [verified stock binary](../.stock/iKeyboard_CK-5200_V122_120.bin), and [custom ELF](/tmp/ck5200-investigation-build/ck5200_qmk.elf).

**→ Recovery is not ready**

The stock download is available. I downloaded 18,648 bytes from the URL in `revert-stock.sh` and verified this hash:

```text
8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8
```

That is a usable source artifact, not proof of recovery. `revert-stock.sh` calls the same `ck5200_usb.py flash` path that failed during your custom attempt. [Restore script](../../../keyboards/ck5200/scripts/revert-stock.sh:53).

There are three distinct cases:

- Stock still runs. The official app is the vendor's documented update route, but an actual restore with this Python tool remains unverified.
- Custom firmware boots and its updater works. The project intends to restore through that updater, but neither the custom USB path nor its flash-writing path has been tested on this keyboard.
- Custom firmware does not boot or enumerate. This project's USB restore cannot reach it. The exact chip, debug pads, boot entry method, protection state, and recovery procedure remain unverified.

Sources: [project recovery limits](../../../keyboards/ck5200/docs/RECOVERY.md:38), [Clicks official app and update guidance](https://app.clicks.tech/en/docs/iphone-get-started).

The downloaded application does not include a verified backup of the boot component, device configuration, or other device-specific regions. There is no device-readback command in the current host tool. Do not treat the stock `.bin` as a complete programmer recovery image.

`bootloader_jump()` only calls `NVIC_SystemReset()`. It is not an independent rescue mode. [Reset hooks](../../../keyboards/ck5200/firmware/platform/ch32v20x/platform.c:60).

**→ What this project actually builds**

The repository root is `/Users/rudimar.ronsoni@feverup.com/src/rudironsoni/clicks-keyboards-custom-firmware`. Its firmware build root is `keyboards/ck5200`. The current branch is `main`. The only initial untracked item was `keyboards/ck5200/.toolchains/`; it remains untouched.

This is a standalone CMake port that compiles selected QMK core files with a custom WCH platform and TinyUSB. It does not use QMK's normal keyboard build machinery. The checked QMK support page does not list CH32V203 as a supported platform. That makes a custom port a real engineering task, not a normal keymap installation. [CMake integration](../../../keyboards/ck5200/CMakeLists.txt:42), [QMK platform list](https://docs.qmk.fm/compatible_microcontrollers).

The runtime path is WCH startup, platform timer and USB clock setup, TinyUSB initialization, QMK initialization, then repeated USB tasks and keyboard scans. The scanner reads a proposed 6-by-6 matrix and QMK applies debounce before sending keyboard reports.

The only keymap assigns A-Z and 1-0 to the 36 matrix positions. It has no normal physical Clicks layout and no configured layers. The matrix-to-physical-key mapping still needs evidence. [Diagnostic keymap](../../../keyboards/ck5200/firmware/keyboard/keymaps/diagnostic/keymap.c:8).

**→ Other faults and gaps to address**

1. **Keyboard reports can be lost.** The platform returns after five milliseconds if the HID endpoint is busy. QMK records the report as its last report before calling this sender. [INFERENCE] A lost key-release report can leave a key held on the host until another changed report succeeds. Keep pending report state until delivery succeeds. [Sender](../../../keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:16), [QMK report cache](../../../keyboards/ck5200/external/qmk_firmware/quantum/action_util.c:285).

2. **USB control behavior is incomplete.** GET_REPORT returns one zero byte regardless of the requested report. SET_IDLE stores a value but no code uses it to resend reports. The descriptor advertises remote wake, but the application never requests it and the pinned USBFS driver's remote-wake function is a TODO. Implement only capabilities that can be tested, and stop advertising unsupported ones. [HID callbacks](../../../keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:58), [descriptor](../../../keyboards/ck5200/firmware/usb/usb_descriptors.c:49), [USBFS driver](../../../keyboards/ck5200/external/tinyusb/src/portable/wch/dcd_ch32_usbfs.c:435).

3. **iPhone behavior is not implemented or proven.** The custom image removes one stock vendor interface and has no Apple accessory session. Stock contains `IAP2-X`, `com.clickscompanion.protocol`, and `com.clickscompanion.app`; the live device emits the handshake above. Plain HID typing might work, but I cannot verify this. Preserve iPhone typing, sleep/wake, modifiers, backlight behavior, and charging as explicit acceptance items. Do not promise that ordinary desktop enumeration proves them.

4. **Several QMK features are absent.** Mouse, extra-key, and NKRO send functions discard their arguments. There is no backlight driver in the owned firmware. EEPROM uses a RAM buffer, so runtime settings do not survive power loss. Compiled keymaps still remain in flash. The current image does not meet the requested full keyboard customization goal. [Host callbacks](../../../keyboards/ck5200/firmware/platform/ch32v20x/protocol.c:28), [EEPROM selection](../../../keyboards/ck5200/CMakeLists.txt:98).

5. **The custom flash writer lacks failure-path evidence.** Page readback is good, but the tests never compile `ck5200_staging.c`. After a page write failure, the code has already advanced `next_offset` and leaves the page full. A caller that continues at the returned offset can index beyond that page buffer. The current host aborts on a rejection, but the firmware should itself latch failure until a new valid session starts. The start operation also leaves any prior on-flash commit marker untouched until the first page fills. The effect of a power loss in that interval depends on the unknown installer. [Staging logic](../../../keyboards/ck5200/firmware/platform/ch32v20x/ck5200_staging.c:41).

6. **`inspect` is not strictly passive.** Its constructor calls `set_configuration()`, may detach a driver, and claims the interface. It does not flash, but it can change USB state. Split descriptor reads from update-session setup. My descriptor check did not use this constructor. [Transport initialization](../../../keyboards/ck5200/tools/ck5200_usb.py:116).

7. **The claimed safe dry run depends on an environment variable.** `flash-qmk.sh` writes if `FLASH=YES` is already set, even without an interactive prompt. Remove this ambiguity from the documented no-write path. [Flash gate](../../../keyboards/ck5200/scripts/flash-qmk.sh:22).

8. **The hardware notes lack their source evidence.** They claim analysis of an Android updater and captured USB traffic, but the tracked repository contains neither the capture nor a reproducible analysis of it. The exact chip and physical matrix mapping remain unverified. Attach offsets, capture provenance, and board evidence to each hardware claim. [Hardware notes](../../../keyboards/ck5200/docs/HARDWARE.md:5).

**→ What is good and worth keeping**

- The dependency revisions are pinned. All three local checkouts are clean and match `bootstrap.sh`: QMK `3d4da6de29c8635c9cd232ce456d8fec8d31921b`, TinyUSB `efa3e132235aefa4ef49d31c746c0c5f0c0e0bc9`, WCH `baef0054588f4826548429ff4e7a9257d752ef2f`.
- The fresh build reproduces the existing binary byte for byte. It produces BIN, ELF, HEX, and a map. Keep these inspection artifacts and the flash-size bound.
- Stock and compiler downloads have expected SHA-256 values. The stock hash matched a new download in this session.
- The host checks response commands, status, accepted offsets, and committed size. Keep these checks when fixing the real transport. Do not weaken them to make the transfer advance.
- The parser is separate from hardware flash operations. That is a useful boundary for a small test of a full update and one interrupted or rejected update.
- A diagnostic matrix keymap is useful after recovery and boot are safe. Do not present it as a usable daily keyboard layout.
- The docs do disclose that hardware boot and recovery are unproven. Keep that candor, but make it the main entry condition instead of leading with installation instructions.

**→ What should go or shrink**

- Remove the claim that finding USB endpoints proves the update path works. It proves endpoint presence only.
- Remove the quick-install framing until the blocking findings are resolved.
- Consolidate `config.h` and `qmk_config.h`; they duplicate the same configuration. CMake force-includes the former while USB descriptors include the latter.
- Remove or explicitly integrate `keymaps/diagnostic/rules.mk`. CMake does not read it, so changing its feature flags does not configure this build.
- Keep `make_stock_probe.py` out of the recovery path. It creates modified firmware, and testing that probe still requires a flash. It does not provide rollback.
- Exclude local `.toolchains/` and stock download caches from version control. The current root `.gitignore` does not cover them; the toolchain is already untracked noise. Do not remove Rudi's installed tools.
- Defer extra feature work until boot, transport, recovery, and basic iPhone typing are proven. Keep the standalone port small; a new framework or multi-model abstraction will not solve these failures.

**→ Verification done and limits**

`bash keyboards/ck5200/scripts/test.sh` exited 0. Output was `3 passed in 0.02s`, followed by `ok` from the C test. The Python tests check packet shape, one status rejection, and one accepted-offset response. The C test exercises only A1 and one A2 packet. Neither suite proves A3, reboot, flash programming, startup, USB enumeration, matrix input, or recovery.

`bash keyboards/ck5200/scripts/build.sh` exited 0 but reused existing output. I then configured and compiled a fresh CMake build in `/tmp/ck5200-investigation-build`, which exited 0. Its image is 18,940 bytes and has SHA-256 `f23d782b15b3b445d540243306cd404a4bc0ebd17f5df4084998cbf6a3053c3d`. Flash usage is 18,940 of 27,136 bytes. Reported RAM usage is 6,184 of 20,480 bytes, including the reserved stack section, not measured peak runtime usage. The build produced upstream declaration, deprecated API, and unimplemented system-call warnings. [Build log](/tmp/ck5200-investigation-build.log).

The first temporary CMake configuration failed because my compiler search-path argument did not reach CMake's nested compiler check. Re-running with the compiler directory in `PATH`, as the repository's build script does, succeeded. This was an investigation command issue, not a project build failure.

Read-only live USB inspection found one `352e:2306` device with `bcdDevice=0x0122`. It has HID interface 2 on `0x81/0x01`, vendor interface 0 on `0x82/0x02`, and vendor interface 1 with endpoints `0x83/0x03` on alternate setting 1. The configuration advertises 100 mA. This confirms current enumeration, not actual typing or flash contents.

Skipped: setup rerun, because installed dependencies were present and matched their pins; current CI status; any custom or stock flash; reset and boot-mode entry; physical teardown; full flash readback; typing and iPhone runtime tests; recovery execution. No new repository tests were added. Final staged and unstaged diffs were empty.

Raw stock disassembly is incomplete for some compressed instruction encodings with the installed disassembler. Address findings above use independently decoded standard 32-bit instructions and stored vector words. I did not treat every printed instruction mnemonic as reliable.

The `Prove It Works` skill principle changed the verification choice: I rebuilt in a fresh directory and inspected the actual stock and custom binaries instead of accepting CI claims or prior build output.

Automatic approval review rejected external `cursor` delegation because it could disclose repository contents to an unverified provider. No child task ran. I completed this investigation locally.

**→ Recommended repair order**

1. Keep the keyboard on stock. Preserve the verified stock application and record device identity. Establish a recovery method independent of the custom application, including the boot component and device-specific regions. If no such method can be verified without destructive steps, stop before flashing.
2. Recover the official update-session behavior from a trace or the official updater. First prove a read-only identity/version exchange. Record raw traffic and distinguish handshake packets from command responses. Do not send A1/A2/A3/A0 without consent.
3. Correct the application address and strengthen static image checks. Reject the current zero-based image for the stock update path. Review startup, interrupt behavior, clocks, and flash operations against the confirmed chip and memory map.
4. Fix report delivery and the custom updater's failure state. Extend the existing tests with the real transport behavior and one key failure path. Keep tests focused on the faults found here.
5. Only after recovery is proven and you explicitly approve a flash, test boot, USB, every physical key, modifier combinations, and restoration. Then prove iPhone typing, sleep/wake, backlight, and charging behavior before adding your normal layout and layers.

No flash is needed to begin steps 2 and 3. Fixing the parser alone is not a safe reason to retry installation.
