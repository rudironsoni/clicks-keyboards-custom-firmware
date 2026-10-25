# Stock characterization v2
Session: 6CC2D0BE-0281-4F62-AFDF-8F08C1CD39BA
Source: physical iOS API observations; not a firmware-image fixture
iOS: 27.0.1. App: 0.2.0 (4).
Status: user-stop
Events: 327. Reads: 0. Validated: 0.
Failed/interrupted: 0.
Replies changed from previous read: 0. Changes can reflect operating conditions.
Response latency range (ms): n/a to n/a.
- Version: 0 validated. Replies:
- Brightness: 0 validated. Replies:
- Backlight delay (ms): 0 validated. Replies:
- Battery idle interval (raw): 0 validated. Replies:
- Raw setting 0x8A: 0 validated. Replies:
- Raw setting 0x8C: 0 validated. Replies:
- Status flags: 0 validated. Replies:

Evidence: iOS callbacks and user-marked conditions, not USB captures.
Key events: 136. Text edits: 97.
Key observation gaps: 4.
Trials: 1 attempts from CK-5200-V122-H120-trials-v1. Coverage includes all untested trials.
Trial completion is not physical validation. See JSON coverage for matrix candidates, label confidence, actions, callback/text/gap event references, and outcomes.
See JSON for timestamps, fragments, before/after state, held keys, and per-read outcomes.
Serial numbers and chip identifiers are omitted as metadata. This report omits typed text.

Unobservable: raw HID reports, matrix crossings, electrical debounce, MFi exchanges,
system-consumed shortcuts, events while suspended, installer state, and independent recovery.
No callback does not prove that firmware sent no event. GCKeyboard is a coalesced source.
R3 independent recovery: UNMET. No firmware write occurs in this mode.
