# CK-5200 Custom QMK v1 behavior contract

This note records the source-to-behavior contract for the software candidate. It is an evidence map, not a test report. The inspected source baseline is commit 131363e2727edd22f4f8483dfc6481f264c70d1c. The iOS profile integration files and tests were uncommitted in the inspection worktree. No tests were run for this note.

## Primary evidence and limits

- Original V122 application: keyboards/ck5200/.stock/iKeyboard_CK-5200_V122_120.bin, SHA-256 8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8. The direct hash and file-type command output identified this exact input as data. The file is a private input symlink and is not included by this note.
- Original disassembly: keyboards/ck5200/.stock/audit/v122.elf and v122.dis. llvm-objdump reported .text linked at 0x2000. Relevant linked code is in the stock HID builder around 0x2a54..0x3010. The direct disassembly at 0x2a54..0x2b90 shows the Shift preamble at 0x2ae6 and the exceptional q/1 path at 0x2b24..0x2b8a. Disassembly at 0x2d8e..0x2e46 shows the config read at 0x2df8 and the Space guard at 0x2e30. Disassembly at 0x2fec..0x3010 shows the r5/PA5 consumer report bit.
- The original descriptor bytes were read from file offset 0x4580 with xxd. They declare consumer usages 0x01ae, 0x029d, and 0x00cf. The descriptor is application data, not proof of what each consumer usage means to iOS.
- The stock extractor keyboards/ck5200/tools/stock_audit/extract_keymap.py reports 34 crossings. It only searches regular entries in 0x2b1c..0x3010. It misses q at r0/PA0 because the first branch is bgez, misses Shift at r2/PA3 because it is handled in the preamble, and mislabels the r5/PA4 sticky-state crossing as Space because it follows the guard for r3/PA5 Space. The 34-row output is a cross-check, not a complete map.
- The primary crossing convention is PB row N selected, then PA bit N sampled. Production scanner source: keyboards/ck5200/firmware/keyboard/matrix.c:26-59. The code and host harness establish software indexing only. Physical traces, switch-to-pin wiring, and keycap labels remain unverified.
- Pinned source revisions recorded in keyboards/ck5200/scripts/pins.env and checked in the dependency worktrees: QMK 3d4da6de29c8635c9cd232ce456d8fec8d31921b; TinyUSB efa3e132235aefa4ef49d31c746c0c5f0c0e0bc9; OpenWCH CH32X035 c6253942b29ff2a3f5e2829182a874afffa45881. The CH32V20x OpenWCH revision is a separate dependency and is not the X035 GPIO implementation.

Stock disassembly addresses below are linked addresses, not file offsets. They identify the relevant branch or entry site. V122 has mode and configuration state, not four fixed QMK keymap arrays. `[INFERENCE]` marks decoded state interactions whose exact user-facing timing or gesture is not established by the application bytes alone. No physical behavior is claimed.

## All 36 electrical positions

The QMK values are the 16-bit values in the compiled default layout. QMK source: keyboards/ck5200/firmware/keyboard/keymaps/diagnostic/keymap.c:24-88. The iPhone mirror is ios/ClicksInspector/Sources/FactoryLayout.swift:11-77. L0 is base, L1 is SYM, L2 is cursor, and L3 is reserved. Numeric values are hex.

Value notes: 0x0001 is KC_TRNS; 0x00e1 is KC_LSFT; 0x00e3 is KC_LGUI; 0x00e0 is KC_LCTL; 0x420d is LT(2, KC_J); 0x5281 is OSL(1). Pinned QMK defines SAFE_RANGE as QK_USER and QK_USER_0..2 as 0x7e40..0x7e42. The keymap maps those three user keycodes to consumer usages 0x00cf, 0x029d, and 0x00b8. The first two stock usages come from the V122 descriptor. Eject 0x00b8 is an intentional QMK change.

| Crossing | V122 linked site | V122 behavior evidence | L0 | L1 | L2 | L3 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| r0c0 | 0x2b24..0x2b8a | q 0x14; SYM 1 0x1e; irregular branch, missed by extractor | 0014 | 001e | 0001 | 0001 |
| r0c1 | 0x2b8e | u 0x18; SYM 7 0x24 | 0018 | 0024 | 0001 | 0001 |
| r0c2 | 0x2baa, 0x2bb0..0x2bc2, 0x2cce..0x2cd8 | k 0x0e; context Down 0x51; SYM quote 0x34 | 000e | 0034 | 0051 | 0001 |
| r0c3 | 0x2bca | Backspace 0x2a | 002a | 0001 | 0001 | 0001 |
| r0c4 | 0x2be0..0x2c0c | Enter 0x28; extra state-byte 0x120 path | 0028 | 0001 | 0001 | 0001 |
| r0c5 | 0x2c10..0x2c1e | Consumer report bit 2; descriptor usage 0x00cf | 7e40 | 0001 | 0001 | 0001 |
| r1c0 | 0x2c20..0x2c3c, 0x2ce0..0x2ce6 | W 0x1a; cursor Up 0x52; SYM 2 0x1f | 001a | 001f | 0052 | 0001 |
| r1c1 | 0x2c40..0x2c5a, 0x2ce8..0x2cf0 | I 0x0c; cursor Up 0x52; SYM 8 0x25 | 000c | 0025 | 0052 | 0001 |
| r1c2 | 0x2c5e..0x2c78, 0x2cf2..0x2cfa | D 0x07; cursor Right 0x4f; SYM semicolon 0x33 | 0007 | 0233 | 004f | 0001 |
| r1c3 | 0x2c7c..0x2c96, 0x2cfc..0x2d04 | L 0x0f; cursor Right 0x4f; SYM quote 0x34 | 000f | 0234 | 004f | 0001 |
| r1c4 | 0x2c9a..0x2cb2 | V 0x19; SYM slash 0x38 | 0019 | 0238 | 0001 | 0001 |
| r1c5 | 0x2cb6..0x2ccc, 0x2d06..0x2d0c | Consumer report bit 1; descriptor usage 0x029d; special chord path also reads r5c4 | 7e41 | 0001 | 0001 | 0001 |
| r2c0 | 0x2d0e | E 0x08; SYM 3 0x20 | 0008 | 0020 | 0001 | 0001 |
| r2c1 | 0x2d2c | O 0x12; SYM 9 0x26 | 0012 | 0026 | 0001 | 0001 |
| r2c2 | 0x2d48 | F 0x09; SYM semicolon 0x33 | 0009 | 0033 | 0001 | 0001 |
| r2c3 | 0x2ae6 | Left Shift is decoded in the preamble by testing PA bit 0x08; not a regular table entry | 00e1 | 0001 | 0001 | 0001 |
| r2c4 | 0x2d64 | B 0x05; SYM 1 0x1e with fixed stock Shift | 0005 | 021e | 0001 | 0001 |
| r2c5 | 0x2d7e..0x2d8c | Left GUI modifier, report modifier bit 0x08 | 00e3 | 0001 | 0001 | 0001 |
| r3c0 | 0x2d8e | R 0x15; SYM 4 0x21 | 0015 | 0021 | 0001 | 0001 |
| r3c1 | 0x2dac | P 0x13; SYM 0 0x27 | 0013 | 0027 | 0001 | 0001 |
| r3c2 | 0x2dc8 | G 0x0a; SYM 9 0x26 with fixed stock Shift | 000a | 0226 | 0001 | 0001 |
| r3c3 | 0x2de4..0x2e08 | Z path reads configuration byte 0x11e at 0x2df8; output is configuration-dependent | 001d | 0001 | 0001 | 0001 |
| r3c4 | 0x2e0c | N 0x11; SYM comma 0x36 | 0011 | 0036 | 0001 | 0001 |
| r3c5 | 0x2e28..0x2e42 | Space 0x2c; suppressed while the r5c4 state control is active | 002c | 0001 | 0001 | 0001 |
| r4c0 | 0x2e46 | T 0x17; SYM 5 0x22 | 0017 | 0022 | 0001 | 0001 |
| r4c1 | 0x2e64..0x2e7e, 0x2f56 | A 0x04; context Left 0x50; SYM minus 0x2d | 0004 | 002d | 0050 | 0001 |
| r4c2 | 0x2e82 | H 0x0b; SYM 0 0x27 with fixed stock Shift | 000b | 0227 | 0001 | 0001 |
| r4c3 | 0x2e9e | X 0x1b; SYM 7 0x24 with fixed stock Shift | 001b | 0224 | 0001 | 0001 |
| r4c4 | 0x2eba | M 0x10; SYM dot 0x37 | 0010 | 0037 | 0001 | 0001 |
| r4c5 | 0x2ed6..0x2eee | Left Ctrl modifier, report modifier bit 0x01; extra state-byte 0x120 path | 00e0 | 0001 | 0001 | 0001 |
| r5c0 | 0x2ef0 | Y 0x1c; SYM 6 0x23 | 001c | 0023 | 0001 | 0001 |
| r5c1 | 0x2f10..0x2f2a, 0x2f72..0x2f7a | S 0x16; context Down 0x51; SYM slash 0x38 | 0016 | 0038 | 0051 | 0001 |
| r5c2 | 0x2f2e..0x2f48, 0x2f7c..0x3010 | J 0x0d in the base path; context Left 0x50; reads configuration byte 0x11f | 420d | 0001 | 0001 | 0001 |
| r5c3 | 0x2fd4..0x2fe8 | C 0x06; SYM 2 0x1f with fixed stock Shift | 0006 | 021f | 0001 | 0001 |
| r5c4 | 0x2b2a..0x2b60; state path also at 0x2876 | No ordinary HID usage established. State logic checks r5/PA4 and r2/PA5; extractor's Space label is a false match from r3c5 guard. `[INFERENCE]` sticky/SYM state control; exact stock gesture not established. | 5281 | 0001 | 0001 | 0001 |
| r5c5 | 0x2fec..0x2ffc | Consumer report bit 0; descriptor usage 0x01ae | 7e42 | 0001 | 0001 | 0001 |

## Stock behavior and QMK changes

- Stock behavior: the compiled V122 bytes and disassembly establish ordinary HID usages, modifier bits, the consumer descriptor values, cursor-context paths, and reads of state bytes 0x11e, 0x11f, and 0x120. Stock is stateful. The V122 application binary does not contain the device's current stored configuration, so exact configured Z/J behavior is `[UNVERIFIED]` from this file alone.
- QMK behavior: the production keymap represents the base, symbol, and cursor behavior as four QMK layers. QMK encodes stock's eight fixed shifted SYM outputs as Shift-modified keycodes. It maps the held cursor mode to LT(2, KC_J), and the editor's sticky SYM control to OSL(1). L3 is a blank transparent editor layer. These are implementation choices, not claims about four stock layers.
- Intentional change: r5c5 uses QMK user keycode 0x7e42 for Consumer Eject 0x00b8. V122 maps that report bit to descriptor usage 0x01ae. The meaning of 0x01ae to a host is `[UNVERIFIED]`; the Eject choice is specific to the custom implementation.
- Intentional omission: QMK uses static Z and LT(2, KC_J) mappings. It does not reproduce the V122 configuration-dependent 0x11e/0x11f behavior or the stock 0x120 state paths.
- Physical mapping: no keycap, typing-position, or switch identity is assigned by this note. All names above identify electrical crossings only.

## Source-to-behavior verification matrix

The expected HID bytes below use the 10-byte report captured by the host harness: keyboard modifiers at byte 0, reserved byte 1, six key slots at bytes 2..7, and the little-endian consumer field at bytes 8..9. This describes deterministic software expectations, not physical USB captures. Test references identify assertions present in source. They do not mean those tests passed in this inspection.

| Behavior | Production path | Deterministic expected result | Existing assertion source | Gap or failure counterexample |
| --- | --- | --- | --- | --- |
| Factory base keys and modifier positions | production matrix scanner -> pinned QMK matrix/action/keymap -> host report; keymap at keymaps/diagnostic/keymap.c | r0c0 q press `[00 00 14 00 00 00 00 00 00 00]`, release all-zero; r2c3 Shift + q has modifier byte `02`; r2c5 GUI + r4c5 Ctrl + q has modifier byte `09` | tests/ck5200_protocol_task_test.c:335-370 checks all 36 matrix crossings, modifiers, and report release; tests/ck5200_qmk_core_test.c:164-192 checks q and LGUI+Q | The expected crossing table is a separate literal in P7, but no mutation control was found or run. The fake GPIO cannot verify board wiring. |
| Symbol activation and return | QMK OSL(1), pinned action layer processing, dynamic keymap layer 1 | Tap r5c4, then r0c0 gives `1` usage 0x1e, then a later r0c0 gives base `q` 0x14. The eight stock shifted SYM keys add Shift modifier 0x02 to their usage. | tests/ck5200_qmk_core_test.c:336-380 tests tap, consume, hold, and release; tests/ck5200_protocol_task_test.c:372-382 checks eight shifted outputs | The QMK interpretation is intentionally an OSL adaptation. There is no independent proof that the stock state-control gesture is equivalent. A wrong QMK one-shot build previously produced base Q instead of symbol; current config keeps the pinned one-shot path enabled. |
| LT tap versus hold | pinned QMK action_tapping.c and action.c, key r5c2 has 0x420d | 80 ms tap emits J usage 0x0d and returns empty; hold beyond the configured 200 ms test interval activates L2; releasing LT while an arrow remains held does not release the arrow early | tests/ck5200_qmk_core_test.c:194-222 and :313-334 | Tests exercise 80 ms and 220 ms intervals, not the exact tapping-term boundary. A tap/hold mismatch at the boundary remains unverified. |
| Cursor layer and native arrows | LT(2, KC_J) hold -> QMK layer 2 -> KC_UP/DOWN/LEFT/RIGHT | L2 arrows are usages Up 0x52, Down 0x51, Right 0x4f, Left 0x50; release arrow then release LT yields empty report | tests/ck5200_protocol_task_test.c:389-398 checks seven arrow positions; tests/ck5200_qmk_core_test.c:313-334 checks held-arrow release across layer change | No board timing or physical switch debounce result. L2 is a QMK held layer, unlike V122 runtime cursor state. |
| Consumer press and release | keymap process_record_user -> ck5200_consumer_key -> consumer report pack -> production protocol queue | 0x00cf is bytes `cf 00`, 0x029d is `9d 02`, custom Eject 0x00b8 is `b8 00`; each release emits 0x0000 | tests/ck5200_qmk_core_test.c:422-445; tests/ck5200_protocol_task_test.c:528-597 | P10/P11 use a TinyUSB completion stub. They verify software ordering, not the pinned X035 DCD or a real host. Stock's 0x01ae is intentionally changed. |
| Simultaneous keys and modifier chords | QMK action report plus six-key rollover; protocol report queue | Six ordinary keys fit the 6KRO report; GUI+Ctrl+Q produces modifier byte 0x09 and q usage 0x14; releasing all keys produces all-zero report | tests/ck5200_qmk_core_test.c:179-192 and :270-310; tests/ck5200_protocol_task_test.c:365-370 | The harness injects non-ghosting matrix states directly. It cannot establish whether the physical matrix ghosts or how the board is wired. |
| HID backpressure and interrupted key sequence | TinyUSB report-ready/complete callbacks -> production protocol queue and retry | With Q press accepted then endpoint busy, consumer Eject press/release queues in order; eventual reports are Q, Q+Eject, Q, then empty after Q release. A failed Q tap retries press before release. | tests/ck5200_protocol_task_test.c:413-482 and :484-570 | Host tests stub completion and busy state. No actual TinyUSB DCD execution is part of this assertion path. |
| Layer change during active sequence | pinned QMK layer resolver and action release processing | A held L2 arrow remains held when LT is released, then clears on arrow release; a held OSL symbol key releases cleanly after the layer key is released | tests/ck5200_qmk_core_test.c:313-380 | Tests cover these sequences in a host harness. No independent mutation/fault run was performed here. |
| Firmware default restoration | command 0x93 -> dynamic_keymap_reset -> compiled keymaps; Swift controller reads all 144 values | After reset, all 144 values equal the compiled factory layout and base r0c0 emits q again | firmware handler keyboards/ck5200/firmware/platform/ch32x035/ck5200_keymap.c:108-111; iOS assertions in ios/ClicksInspector/Tests/QMKProfileTests.swift:195-202 and 269-277 | Reset reply alone is insufficient. Existing source test ignores a 0x93 reset and expects full-map comparison to fail. This uses transient keyboard RAM, not keyboard nonvolatile storage. |
| Swift edit affects QMK behavior | production Swift controller -> framed 0x92 and 0x91 -> C keymap handler -> dynamic keymap -> QMK processing | Change r0c0 from q to usage 0x04, read back it, then actual QMK press report changes to `[00 00 04 00 00 00 00 00 00 00]`; release is empty | ios/ClicksInspector/Tests/QMKProfileTests.swift:132-202 | QMKBridge.c:83-85 calls ck5200_keymap_handle directly. It bypasses protocol.c dispatch and TinyUSB. The separate C protocol harness does not run the Swift controller through protocol.c. This is a material integration boundary to close or state in final evidence. |

## Profile and failure-state assertions

Profile source in ios/ClicksInspector/Sources/KeymapProfile.swift validates schema 1, CK-5200 model, hardware revision 1.2.0, four 6x6 layers, non-empty bounded names, and writes JSON atomically. CustomFirmwareController requires model CK-5200, hardware revision 1.2.0, and the exact custom version payload `QM 00 01` before editing; the full C response is `08 02 03 00 51 4d 00 01`. It also checks the 0x90 response declares 4 layers, 6 rows, and 6 columns. The controller reads all 144 values, writes one key at a time, then reads each written key back. It reports RAM state as RAM state. Reconnection reads the current keyboard map and compares it to the saved phone profile; applying the selected profile is explicit. QMKProfileTests.swift:204-222 asserts that saved JSON survives a controller relaunch, reconnect detects a difference without auto-writing, and explicit apply verifies the intended full map.

The following failure assertions are present in ios/ClicksInspector/Tests/QMKProfileTests.swift. They are source findings only, not test results from this inspection.

| Fault injected | Asserted resulting state |
| --- | --- |
| Lose a write reply after the C handler applied it | Only confirmed writes count; keymap verification is cleared; controller requires reconnect; reconnect reads the actual changed RAM; explicit resume applies the remaining differences. Lines 230-249. |
| Reset during profile application | Controller cannot report a complete profile; reconnect exposes factory and partial values; manual reapply is required. Lines 251-267. |
| Ignore reset command but return acknowledgment | Full 144-value comparison rejects reset success; prior profile remains in QMK state. Lines 269-277. |
| Fragment a read response or close the link after reset/write | Operation times out or becomes unverified; last write is marked possibly applied; reconnect reads actual C/QMK state. Lines 279-294. |
| Corrupt a readback | Keymap verification remains false and the mismatch is shown. Lines 295-301. |
| Stale close or fragment callback from an old generation | Current session identity and keymap verification remain intact. Lines 223-228. |
| Wrong firmware identity, model, or hardware revision | Custom info/read/write/reset requests are rejected or not sent. Lines 315-334. |
| Malformed or out-of-range keymap command traffic | The production C handler is fed a deterministic 200,000-packet fuzz sequence, including framing and argument errors; the harness asserts bounded response size and then reads all 144 positions. The current assertion does not independently prove each invalid write left its target unchanged. tests/ck5200_qmk_core_test.c:224-268. |
| Unsupported profile schema, malformed dimensions, or corrupt JSON | Profile validation/load fails; corrupt or unsupported file contents are preserved; no successful save is shown. Lines 336-360. |

The CKeyboardLink fault fixture calls the production C handler for normal commands and injects response loss, reset, fragmentation, read corruption, or ignored acknowledgments around it. The actual resulting state is QMK transient memory in the host process. It does not prove accessory transport timing or device RAM behavior.

## Independent verification boundary

- keyboards/ck5200/scripts/test.sh:78-130 compiles pinned production QMK core with the CK-5200 keymap and handlers. The protocol harness additionally links production firmware/keyboard/matrix.c and firmware/platform/ch32x035/protocol.c. It uses a TinyUSB completion stub, not TinyUSB's device controller.
- ios/ClicksInspector/Tests/build-qmk-bridge.sh compiles the same pinned QMK core, keymap, dynamic keymap, transient EEPROM, and production CK-5200 C handlers for the Swift test bridge. QMKBridge.c uses a synthetic matrix array and captures reports. SimulatedQMKLink uses that bridge in the simulator.
- TinyUSB source checked at the pinned revision defines endpoint readiness and transfer-completion behavior in external/tinyusb/src/class/hid/hid_device.c:88-105, 367-386, and external/tinyusb/src/device/usbd.c:1660-1677, 1722-1728. The current host harness does not execute the pinned DCD code.
- OpenWCH CH32X035 GPIO source implements input reads via INDR and output set/reset via BSHR/BSXR/BCR in external/ch32x035/EVT/EXAM/SRC/Peripheral/src/ch32x035_gpio.c:270-276 and :335-355. The matrix host harness substitutes GPIO. This confirms which registers the pinned source uses, not electrical behavior on a keyboard.
- ios/ClicksInspector/UITests/QMKProfileUITests.swift:3-76 specifies simulator UI steps for edit, save, app relaunch, mismatch detection, explicit reapply, restore, and reconnect. This file is a test specification; no simulator result is claimed here.
- Expected HID tables are mirrored in firmware, Swift, and test source. P7 has an explicit base-layer literal, while the 144-position Swift behavior test derives expected keycodes from FactoryLayout. No mutation-control result was present in the inspected sources or run here. To establish independence, final review should mutate one compiled default keycode and show the production-path expected-HID test fails, then rebuild and verify the exact final source.
- Software-only evidence can establish pinned QMK action processing and deterministic captured HID reports. It cannot establish physical keycap mapping, electrical matrix behavior, TinyUSB DCD timing, iPhone accessory behavior, or actual keyboard state after a physical reset. No hardware behavior is claimed.
