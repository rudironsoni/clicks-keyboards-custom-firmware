# Stock characterization v2
Session: EE1FEF37-4BCC-4990-8540-6C2BD117536E
Source: physical iOS API observations; not a firmware-image fixture
iOS: 27.0.1. App: 0.2.0 (4).
Status: user-stop
Events: 105. Reads: 21. Validated: 21.
Failed/interrupted: 0.
Replies changed from previous read: 0. Changes can reflect operating conditions.
Response latency range (ms): 1.07 to 19.38.
- Version: 3 validated. Replies: 08 02 03 00 01 20 01 22
- Brightness: 3 validated. Replies: 05 02 84 00 ff
- Backlight delay (ms): 3 validated. Replies: 06 02 86 00 4e 20
- Battery idle interval (raw): 3 validated. Replies: 06 02 88 00 00 b4
- Raw setting 0x8A: 3 validated. Replies: 05 02 8a 00 00
- Raw setting 0x8C: 3 validated. Replies: 05 02 8c 00 21
- Status flags: 3 validated. Replies: 05 02 8d 00 00

Evidence: iOS callbacks and user-marked conditions, not USB captures.
Key events: 0. Text edits: 0.
Key observation gaps: 4.
Trials: 1 attempts from CK-5200-V122-H120-trials-v1. Coverage includes all untested trials.
Trial completion is not physical validation. See JSON coverage for matrix candidates, label confidence, actions, callback/text/gap event references, and outcomes.
See JSON for timestamps, fragments, before/after state, held keys, and per-read outcomes.
Serial numbers and chip identifiers are omitted as metadata. This report omits typed text.

Unobservable: raw HID reports, matrix crossings, electrical debounce, MFi exchanges,
system-consumed shortcuts, events while suspended, installer state, and independent recovery.
No callback does not prove that firmware sent no event. GCKeyboard is a coalesced source.
R3 independent recovery: UNMET. No firmware write occurs in this mode.
