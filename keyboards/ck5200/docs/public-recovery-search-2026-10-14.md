# CK-5200 public recovery search, 2026-10-14

## Result

Two new leads emerged: a public Clicks USB test application with a vendor-command-table history, and an iPhone 15 Plus report of a `123` + spacebar connection-recovery sequence. Neither source demonstrates recovery when the keyboard application cannot execute. No checked source supplied a CK-5200 boot image, full flash dump, or independently reachable recovery service.

Follow-up: the [offline audit](recovery-audit-2026-10-14.md#reported-123--space-combination) traced the key combination and inspected ClicksFT history.
The stock Space branch suppresses its output while the layer crossing is held. No independent rescue service was found in those paths.
The results below preserve the original public-search scope.

This result is bounded by the sources below. Search results, empty API lists, HTTP failures, and missing documentation do not prove that an artifact or recovery mode does not exist.

## 1. New public source: ClicksFT

Repository: <https://github.com/louis-hsu/ClicksFT>.
Inspected revision: `f93111b7434a34ead80604843204d062d1365c2c`.
GitHub describes it as "Android app for Clicks keyboard firmware test". The package is `com.fxtec.clicksft`. These facts do not establish that Clicks officially endorses or maintains this repository.

### Verified from source

- [README](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/README.md): its stated purposes include checking keyboard command responses and testing firmware upgrades. However, the Firmware section explicitly says "Not implemented yet." Its TODO refers to comparison with the vendor's command documentation.
- [UsbDeviceHandler.kt](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/app/src/main/java/com/fxtec/clicksft/UsbDeviceHandler.kt): selects USB VID `13614` (`0x352E`), PID `8966` (`0x2306`), interface index `1`, input endpoint index `0`, and output endpoint index `1`. It uses Android `UsbManager` and `bulkTransfer`. Its source comment says the constants are defined by the device vendor. Endpoint indices are not endpoint addresses.
- [FirmwareTabView.kt](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/app/src/main/java/com/fxtec/clicksft/FirmwareTabView.kt): only displays interface information from `printInterfaceInfo()`. It contains no image transfer, boot-entry action, or recovery retry.
- [default_commands.xml](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/app/src/main/res/raw/default_commands.xml): contains the `0x81` through `0x8D` settings/event command family. It includes `0x83` masks for `Accs. Stop`, `LEDSwitch`, `TabToCtrl`, and `CursorMode`. It contains no flash-read or boot-recovery command.
- [clicks_commands.xml](https://github.com/louis-hsu/ClicksFT/blob/f93111b7434a34ead80604843204d062d1365c2c/clicks_commands.xml): contains the same command family, with much of the example table commented out. It is not an additional service-command table.
- The [commit history](https://github.com/louis-hsu/ClicksFT/commits/main/) includes `a85d4c9b14b899bb648ea37b6079dbacf75e8b9d`, whose message says "Add current official commands table (v1.08)". This is the author's provenance claim, not proof that the table covers every command or the CK-5200 hardware revision.

### Recovery value and limits

[INFERENCE] This is a useful public provenance route for the protocol: a test application built around vendor documentation can help distinguish a configuration command from a claimed recovery command. It does not expose a second boot transport. USB enumeration and a functioning keyboard-side command handler are still prerequisites in this implementation.

The current recursive tree contained no `.bin`, `.hex`, `.elf`, or `.ipa` firmware artifact. GitHub returned empty releases, tags, forks, and all-state issue lists. The branch API returned:

| Branch | Commit |
| --- | --- |
| `main` | `f93111b7434a34ead80604843204d062d1365c2c` |
| `commands` | `495e1aff879107b40534c5ff81ee556601e4ca9d` |
| `separation` | `946997ac2cdc808221e02b707172ad869f9ea89f` |
| `testplan` | `1f5d47253563512cdf51bd9efc75849ebddc1d7a` |

All four branch tips appeared in the returned main-branch commit history. Historical blobs were not exhaustively inspected. The USB PID does not, by itself, identify the test unit as CK-5200 hardware `1.2.0`.

## 2. New key-combination lead: `123` + spacebar

An indexed [iPhone 15 Plus connection-failure thread](https://www.reddit.com/r/ClicksKeyboard/comments/1gyenh9/my_replacement_clicks_keyboard_isnt_working_or/) contains a report of removing the case, pressing `123` and spacebar, reinserting the phone, connecting a charger, and pressing the combination again. Search results also describe the reported cause as a stuck data-passthrough setting.

**[UNVERIFIED]** Only search-index excerpts were available. Direct requests to the normal and `old.reddit.com` pages returned a generic Reddit page, not the complete discussion. The poster's device, firmware version, exact timing, and result cannot be independently verified from the fetched page.

A separate indexed [iPhone 15 Plus charging-failure thread](https://www.reddit.com/r/ClicksKeyboard/comments/1m85431/clicks_iphone_15_plus_no_longer_receiving_power/) reports trying the "123 + spacebar trick" without success. That result is also an unverified search excerpt.

There is a concrete model discrepancy worth resolving offline:

- The [official iPhone guide](https://learn.clicks.tech/knowledge-base/iphone-get-started.md) assigns `123` + spacebar to USB-C data mode on **iPhone 16** Clicks.
- The [official support page](https://learn.clicks.tech/not-a-part-of-google-sheet/support.md) says iPhone 15 USB-C models support charging through the lower connector, while iPhone 16 adds data passthrough.
- The Clicks-branded [18-page user guide](https://m.media-amazon.com/images/I/C1qiS-cpt9L.pdf), page 12, explains the key combination as data-mode entry. Page 17 explicitly lists USB-C data mode as an iPhone 16 addition unavailable on iPhone 15 or 14.

[INFERENCE] The Reddit report could concern a connection-state fault, a model mix-up, or an undocumented behavior. It is not evidence of a bootloader key chord. Check whether stock V121/V122 recognizes this combination and whether its effect is confined to application code. A handler found only in the application cannot establish independent recovery. No physical key test was performed.

## 3. Public documentation now has a complete discovery index

The official learning site exposes <https://learn.clicks.tech/llms.txt>. It lists the iPhone, Pixel, Razr, and Power Keyboard guides, troubleshooting, support, and migrated blog posts. The index is a better starting point than guessing old support URLs.

Checked pages and relevant results:

| Exact source | Result |
| --- | --- |
| <https://learn.clicks.tech/> | Links the documentation index and model-specific guides. |
| <https://learn.clicks.tech/llms.txt> | Full returned index reviewed for recovery, reset, firmware, and troubleshooting leads. |
| <https://learn.clicks.tech/knowledge-base/iphone-get-started.md> | Normal app updates; data-mode chord identified as iPhone 16. Exposes firmware tutorial video `ZN6Y_N3BZ3Q`. The video itself was not viewed. |
| <https://app.clicks.tech/en/docs/iphone-get-started> | Older guide route also describes normal app firmware updates. |
| <https://learn.clicks.tech/not-a-part-of-google-sheet/support.md> | Model-specific data-passthrough distinction; `123` + globe is a backlight toggle. No independent recovery procedure in the returned page. |
| <https://learn.clicks.tech/knowledge-base/kb-power-keyboard-troubleshooting.md> | Power Keyboard pairing and charging fixes. `Sym` + `0` clears a pairing slot. This is a different product, not CK-5200 recovery. |
| <https://discover.clicks.tech/clicks-keyboard-app-v12-introduces-cursor-mode-clicks-key-customization-and-more> | Clicks' 2024-08-29 announcement requires an inserted phone for CHECK FIRMWARE. Account restore is described as restoring settings, not executable firmware. |
| <https://m.media-amazon.com/images/I/C1qiS-cpt9L.pdf> | All 18 pages read. Page 13: account backup/restore covers settings. Page 15: checking firmware requires the keyboard connection and an iPhone Clicks account. No recovery chord or boot image is supplied. |

The PDF is Clicks-branded but hosted on Amazon. Its SHA-256 is `ad5861f9723f03086da3529d91a3806f61ffd90de2c237ca15fe8624d316a15e`. It was read from a temporary download, not added to the repository.

## 4. Other technical sources and archive checks

| Exact source | Result and relevance |
| --- | --- |
| <https://bkovac.github.io/modem-thing/> | First-person hardware project using Clicks for **iPhone 16 Pro Max** with Linux. Author says the keyboard MCU is "CH32V203 or similar" and does not report flashing it. Its EDL recovery and partition-backup discussion concerns the MF800 modem, not Clicks. No CK-5200 dump or recovery proof. |
| [Pastiera ClicksFirmwareVersionReader.kt](https://github.com/palsoftware/pastiera/blob/e7d8f27ecc7253e61690b5d34f110b25dc68bb16/app/src/main/java/it/palsoftware/pastiera/ClicksFirmwareVersionReader.kt) | Reads the standard Bluetooth GATT firmware revision characteristic `00002a26-0000-1000-8000-00805f9b34fb`. This is a Bluetooth product path, not a CK-5200 USB boot service or manifest. |
| <https://archive.org/advancedsearch.php?q=%22clicks+keyboard%22&output=json> | Returned `numFound: 0` for this exact Internet Archive item-metadata query. This does not search every archived web URL or prove no firmware archive exists. |
| <https://web.archive.org/cdx/search/cdx?url=xinyi1.clicks.tech/*&output=json&filter=statuscode:200&collapse=urlkey> | HTTP 503. Archive contents could not be checked. |
| <https://web.archive.org/cdx/search/cdx?url=*.clicks.tech/*&output=json&filter=mimetype:application/octet-stream&collapse=urlkey> | Request timed out. No conclusion about archived binaries. |
| <https://device.report/clicks/ck-5200> | HTTP 403. Certification/manual contents could not be checked. |
| <https://www.reddit.com/r/ClicksKeyboard/comments/1uj31tu/any_way_to_hard_reboot_the_power_keyboard/.json> | HTTP 403. The search result concerns Power Keyboard, not CK-5200. |

Public GitHub code search for `"CK-5200"` returned this project's own files. They were excluded as circular evidence. Searches for `"CK5200"`, `"xinyi1.clicks.tech"`, `"iKeyboard" "CH32"`, and `"firmwares" "clicks.tech"` returned no code results in those queries. Broader repository searches found ClicksFT; searches for Clicks firmware code found Pastiera. Neither provided an iOS manifest or boot image in the inspected material.

Other web searches included exact `"iKeyboard_CK"`, `"CK-5200" firmware bootloader`, `"Clicks keyboard" "bootloader"`, `"Clicks keyboard" "reflash"`, `"Clicks keyboard" "bricked"`, and targeted reset/recovery searches in `r/ClicksKeyboard`. Search-result snippets from unrelated products were not treated as CK-5200 evidence.

### Exact GitHub API scope

These read-only endpoints were checked with `gh api`:

- `https://api.github.com/repos/louis-hsu/ClicksFT`
- `https://api.github.com/repos/louis-hsu/ClicksFT/commits/HEAD`
- `https://api.github.com/repos/louis-hsu/ClicksFT/git/trees/HEAD?recursive=1`
- `https://api.github.com/repos/louis-hsu/ClicksFT/branches?per_page=100`
- `https://api.github.com/repos/louis-hsu/ClicksFT/commits?per_page=100`
- `https://api.github.com/repos/louis-hsu/ClicksFT/releases`
- `https://api.github.com/repos/louis-hsu/ClicksFT/tags?per_page=100`
- `https://api.github.com/repos/louis-hsu/ClicksFT/forks?per_page=100`
- `https://api.github.com/repos/louis-hsu/ClicksFT/issues?state=all&per_page=100`
- `https://api.github.com/users/louis-hsu/repos?per_page=100`, public repository names/descriptions only.
- Contents endpoints for the five ClicksFT files linked in section 1 and the Pastiera file linked above.

## 5. Scope and verification

Only this note was added. Existing recovery documents and implementation files were not edited. No device operation, reset, ISP/SDI access, firmware write, proxy/certificate/settings change, or outbound message was performed. Public source reads did not require contacting a maintainer or joining a private community. No Apple transport/API research was repeated.

No runtime test applied to the original documentation-only search. Historical ClicksFT blobs were checked later, as the linked audit records.
The firmware tutorial video and unavailable archive/Reddit contents remain unchecked. This note does not claim a working recovery route or change flash authorization.
