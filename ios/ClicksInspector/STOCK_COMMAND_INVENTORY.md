# CK-5200-V122-H120-v1 command inventory

Scope: model `CK-5200`, hardware `1.2.0`, firmware `1.2.2`.
Image: `iKeyboard_CK-5200_V122_120.bin`, 18648 bytes.
SHA-256: `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`.

This inventory covers every explicit branch in the application dispatcher at linked address `0x3216`.
It does not cover the unread installer or other firmware revisions.
The seven demonstrated packets are the entire executable M1 allowlist in `Sources/StockReadCommand.swift`.
Authorization compares exact packets and all three revision fields. There is no opcode scan or argument generator.

| Command | Classification | Static evidence (linked address) | M1 execution |
| --- | --- | --- | --- |
| `03` | Demonstrated read-only | `396e`: fixed version response | `02 03` |
| `08` | Demonstrated read-only diagnostic, exact packet only | `335e..3408`: bounded local CRC work, declared reply length 4 | Disabled. Historical packet `05 08 00 00 00` returned `04 02 08 00`. No checksum or memory data. |
| `81` | State-changing or settings-writing | `3422..345a`: brightness call `3010`, updates config `11d` and dirty flag | Disabled |
| `82` | State-changing or settings-writing | Same brightness call, branches past persistent field update at `342c` | Disabled, even when a change is runtime-only |
| `83` | State-changing or settings-writing | `3472..35c4`: masked field `120`, dirty flags and peripheral call `3050` | Disabled |
| `84` | Demonstrated read-only | `35da`: config `11d` | `02 84` |
| `85` | State-changing or settings-writing | `32ba`, `35fc`: config `124`, dirty flags | Disabled |
| `86` | Demonstrated read-only | `3634`: config `124` | `02 86` |
| `87` | State-changing or settings-writing | `365e`: config `126`, dirty flags | Disabled |
| `88` | Demonstrated read-only | `36c6`: config `126` | `02 88` |
| `89` | State-changing or settings-writing | `32fc..3328`: config `11f`, dirty flag | Disabled |
| `8a` | Demonstrated read-only | `3706`: config `11f` | `02 8a` |
| `8b` | State-changing or settings-writing | `371c`: config `11e`, dirty flag | Disabled |
| `8c` | Demonstrated read-only | `375c`: config `11e` | `02 8c` |
| `8d` | Demonstrated read-only | `333e`: two runtime flags, no writes except reply buffer | `02 8d` |
| `a0` | Update, reboot, flash, or protection-changing | `3954`: reboot path | Disabled |
| `a1` | Update, reboot, flash, or protection-changing | `37a8`: initialize update staging state and image size | Disabled |
| `a2` | Update, reboot, flash, or protection-changing | `380a`: offset checks, RAM staging, flash writer `3174` | Disabled |
| `a3` | Update, reboot, flash, or protection-changing | `3782`, `38de`: flush staging, write size header through `31b2` | Disabled |
| `8f` | Unknown command semantics, demonstrated unsupported on V122 | Default `328a`: status `ff`, four zero payload bytes | Disabled. Historical `06 8f 00 00 00 00` returned `08 02 8f ff 00 00 00 00`. Not a getter. |
| Every other opcode, including APK `11`, `12`, `8e`, `90`, `91`, `92`, `93` | Unknown | No explicit handler in this dispatcher. Default reply does not establish safety for arbitrary packets. | Disabled |

**Statically analyzed read-only, not yet demonstrated:** no additional command qualifies in this inventory.
Malformed lengths and alternate `08` selectors are outside the demonstrated packet contract.
No protection-changing command was identified separately from the update class. This is not proof that no other interface exists.

Sources:

- [Physical getter evidence and bounded service probes](../../keyboards/ck5200/docs/USB_AND_IPHONE.md#stock-ios-session-and-read-command-investigation-2026-10-08).
- [Stock decode and disassembly method](../../keyboards/ck5200/docs/STOCK_DECODE.md).
- Local `keyboards/ck5200/.stock/audit/v122.dis`, dispatcher `3216..399a`, rechecked for this inventory.

The supplied screenshots demonstrate parsed stock replies, not an independent wire capture.
New evidence bundles contain only observations delivered during the recording session.
Downloaded image bytes are not inserted as observed replies.
