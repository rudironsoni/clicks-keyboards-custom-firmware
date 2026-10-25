# Selective factory-ISP recovery inquiry

Status: **READY TO SEND, UNSENT**. Date: 2026-10-17. Updated: 2026-10-20 with
the OEM boot-stage questions after the
[memory-ownership investigation](memory-ownership-and-oem-transition.md).
Recipient: `support@clicks.tech`, Clicks support/firmware engineering, with the protocol questions forwarded to WCH ISP engineering.
The [existing recovery audit](../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md) records this public support address.
This draft requests existing records. Sending it requires a separate user decision.

## Exact dependency

WCH must specify the target loader's permitted erase/program effects for the applicable CH32X035 revision.
Clicks must identify that revision and define the OEM storage layout, tail policy, and installer startup behavior.
A host API or a generic development-board result cannot supply both contracts.

## Message

**Subject: CK-5200 hardware 1.2.0: application-only factory ISP recovery and flash preservation**

Hello Clicks firmware engineering,

I own one working CK-5200 for iPhone 15 Pro Max, hardware 1.2.0.
I am assessing recovery when the application cannot boot, before any custom firmware installation.
Please forward the factory-protocol questions to WCH ISP engineering if necessary.
I request existing documentation or engineering evidence, not experiments on my keyboard.

The official V122 file is `iKeyboard_CK-5200_V122_120.bin`, 18,648 bytes.
Its SHA-256 is `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.
It occupies main-flash offsets `[0x2000,0x68D8)`, with physical base `0x08000000`.
The presumed OEM installer occupies `[0x0000,0x2000)` and is absent from this file.

The recovery method must leave that installer untouched, including during interruption.
It must also preserve other unknown flash, serial/configuration pages at offsets `0xF600` and `0xF700`, System FLASH, options, and authentication data.
It must not require mass erase, protection removal, debug enabling, or replacement of the factory loader.

**0. The OEM boot stage at offsets `[0x0000,0x2000)` (decisive questions)**

Static analysis of all six official application images (V114-V122) shows that the
application stages an image at `0x8A00` (four-byte length marker, then image
bytes), acknowledges A0 with a plain system reset, never reads the marker, and
contains no destination-write code. The boot stage must therefore consume the
marker and perform the installation copy. Please answer from existing
engineering records for CK-5200 hardware 1.2.0:

0a. **Marker validation:** Which marker values does the boot stage accept, and
    what length bounds does it enforce? Does it validate the staged image
    beyond the marker (checksum, signature, version window, model check), and
    what does it do on validation failure?
0b. **Marker clearing order:** Is the marker cleared or invalidated before,
    during, or only after a completed destination copy? After a successful
    installation, is the marker value `0xFFFFFFFF`, the installed length, or
    something else?
0c. **Interrupted-copy retry:** If power is lost after a valid marker but during
    the destination copy, what does the next power-up do? Does the keyboard
    complete the installation, revert, or remain in its previous state? Is this
    behavior covered by a specified or tested power-loss sequence?
0d. **Maximum image acceptance:** The application accepts any declared length
    up to `0x6A00` (the full `[0x2000,0x8A00)` area). Does the boot stage accept
    and correctly install a `0x6A00`-length image? Does it accept any length the
    application accepts, or only lengths matching officially released images?
0e. **Copy granularity and final pages:** Does the boot stage copy exactly the
    marker length in bytes, or whole 256-byte pages? What does it write to the
    unwritten tail of the final destination page (erase residue `0xFF`, staged
    bytes, or preserved previous content)?
0f. **Recovery with a dead application:** When the application cannot start or
    enumerate USB, what supported method restores firmware from an iPhone? Does
    the boot stage expose any interface in that state, and under which entry
    condition?

Answers to 0a-0f from the installer's source or an engineering specification
would resolve the current installation and recovery assessment directly.

**1. Applicable loader and protection**

Please identify the exact MCU variant, applicable System FLASH loader revision, and BOOT configuration for this hardware revision.
Does factory entry work with the application entirely invalid, without any application-set flag?
What RDPR/WRPR policy applies to shipped CK-5200 units?
Can the proposed operations work under that policy without changing any option byte?

**2. Selective ERASE, independently of PROGRAM addressing**

The public factory-ISP encoding uses A4 with a four-byte erase count.
A5 PROGRAM and A6 VERIFY carry a four-byte address.
An addressable PROGRAM does not establish selective ERASE.

Does this loader expose an operation that erases only selected application pages starting at offset `0x2000`?
Please provide its documented request format, prerequisite state, status meanings, and supported loader revisions.
For A4 specifically, please define the start address, count units, minimum, maximum, rounding, and protected-page behavior.
Is there any previous command or session setting that changes A4's start address?
Please explain whether official `IAPStartAddr` and `IsEraseAllCFlash` settings apply to CH32X035 factory ISP, rather than optional application IAP.
Our static review of `libwch55xisp-4.0.0.so` found a count-only A4 path and no X035 erase-start control from `IAPStartAddr`.
`IsEraseAllCFlash=false` still calls ERASE in that library.
Its SHA-256 is `b73d34967fd13e4f1f71749019666d826601e16c48ee02068196add964cc1bf8`.
If no selective operation exists, please state that explicitly for the applicable revision.

**3. PROGRAM, VERIFY, and finalization effects**

Does A5 accept offset `0x2000` without first programming lower addresses?
Does it erase implicitly, buffer full pages, or alter bytes outside the supplied range?
Please specify alignment, short-packet padding, empty final-packet effects, and any writes triggered by A6 or session closure.
The inspected high-level WCH programming flow also writes configuration before erase. Is there a supported path that omits all configuration writes?
Can A6 compare the exact restored image under the shipped protection policy without modifying other state?
If a RAM helper is proposed, please specify how factory USB loads and starts it without flash writes or protection changes.

**4. The final 40 destination bytes**

The final destination page is `[0x6800,0x6900)`.
V122 supplies its first 216 bytes. It supplies nothing for `[0x68D8,0x6900)`.
Our cross-version analysis indicates these 40 bytes are unused application-area
padding: every consecutive official update requires erasing the predecessor's
final page (V121→V122 has 69 erase-forcing bytes in that page), V122 writes image
data through V121's former tail, no official image's code reads beyond its own
extent, and device-specific data (serial at `0xF600`, configuration at `0xF700`)
lives outside the application area. Please confirm or correct this
classification.

Static analysis of the V122 application shows staging at `0x8A00` with a four-byte length marker.
The staged image ends at `0xD2DC`, followed by 36 `0xFF` fill bytes through `0xD2FF`.
This does not define the 40 destination bytes or the next staging word at `0xD300`.
How does the installer handle the shifted last page?
Does it read and preserve the destination tail, generate padding, or copy beyond the declared image length?

**5. Interruption and normal startup**

Please define the affected regions after power loss during erase, program, buffer flush, or verification.
Does the independent loader remain available after every such interruption?
What documented retry sequence preserves all excluded regions without changing configuration?
After direct application restoration, could an existing staging marker cause the OEM installer to overwrite that application?
How can recovery handle that state without speculative writes to staging or installer storage?

A version-matched protocol specification, reviewed loader/installer excerpt, or existing engineering test record would help.
Please identify any unsupported requirement explicitly.
A successful normal update, generic ACK, or application-only retry does not establish recovery from an unbootable application.

No keyboard opening, probing, reset experiment, ISP command, flash write, or protection change is authorized.
The eventual recovery interface must be controlled from an iPhone.
We will evaluate transport and external hardware only after the preservation contract is established.

Thank you,
Rudi
