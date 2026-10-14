# CK-5200 authoritative readiness record

This file owns the current readiness and authorization status. Other guides must defer to it.

| Gate | Current status |
| --- | --- |
| M1 stock recorder software | Subject to host tests, simulator integration tests, and unsigned iPhone CI |
| M1 physical CK-5200 acceptance | UNVERIFIED, no baseline from this recorder collected |
| R3 iPhone-only independent recovery | UNMET, installer unread; no proven rescue from an unbootable application |
| Custom firmware flashing | NO-GO, no custom image authorized |
| M1 stock firmware writes and resets | Disabled; not authorized for this stage |
| Default iOS Debug and Release | Diagnostics-only; controller permits only exact demonstrated stock getters on the known identity |
| AuthorizedStockUpdate configuration | Retained stock-only implementation for a later separately authorized milestone; not an M1 install configuration |

The goals remain official stock installation, separately authorized custom transitions, independent iPhone-only recovery,
preservation of the only keyboard, and reuse of maintained dependencies.

The custom target is CH32X035. Custom keymap storage is transient RAM and loses edits on power loss.
No flash-backed EEPROM partition is demonstrated. Preserve installer `0x0000–0x1FFF`, application `0x2000–0x89FF`,
staging `0x8A00+`, and upper reserved flash.

An acknowledged transfer, an acknowledged commit, a session disconnect, functional typing, and verified installation
are different observations. None proves independent recovery. A same-version reply cannot prove that installation occurred.
The Mac dispatcher depends on an executable application. It is not a recovery backup for an unbootable image.

Next: [sign/install the diagnostic app and collect the stock baseline](../../../ios/ClicksInspector/SIGNING_AND_BASELINE.md).
No firmware writes, resets, ISP operations, protection changes, or SDI investigation belong to this task.
