# Export UI verification

## Failure and evidence

Hosted run [39050140089](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39050140089) failed in `StockCharacterizationUITests.swift` while parsing UI-captured JSON.
The reported byte offset was `212993`.
The test tapped Next and immediately appended the TextEditor value without checking which page it had captured.

The original xcresult attachment is 299,224 ASCII bytes with SHA-256
`a8f219a2bbc231862a20809b02feea0001b8e1cdbd3f058aebe14d4f3ab49bfe`.
Captured pages 13 and 14 are identical 16,384-byte strings.
Removing that duplicate page makes the diagnostic copy parse successfully.
The test does not perform this removal or repair captured JSON.
The locally recovered attachment matches the official `xcresulttool` export byte for byte.

The original run did not retain its source export file.
Diagnostic run [39051883978](https://github.com/rudironsoni/clicks-keyboards-custom-firmware/actions/runs/39051883978) passed the unchanged UI test, but exposed a collection defect:
`simctl get_app_container` failed because Xcode had shut down the simulator.
That run therefore did not establish source-file equality.

## Correction and acceptance

The preview header now exposes the source page index, page count, UTF-8 length,
and SHA-256. The TextEditor's accessibility value remains the actual displayed text.
The UI test waits for the requested index and matching text bytes before appending
that exact validated snapshot. It visits the declared number of pages.
The regression rejects stale text and stale page identity, while accepting valid
identical adjacent pages. The serializer is unchanged.

An explicit Debug-only UI-test argument preserves the original files before the
deletion test. CI reads only those snapshots from normal simulator storage,
including shutdown devices and test clones. Attachment extraction is independent
of source collection. Temporary prior-run retrieval was removed.

`Tests/compare_ui_export.py` requires three valid source exports and three unique,
byte-identical UI captures. Missing, duplicate, malformed, or different captures
fail the workflow. Reports include source and capture hashes and the first mismatch.

Acceptance requires the full hosted workflow, including that comparison and stock-manager packaging.
Local Xcode checks were unavailable because this Mac's required wrapper commands were absent.
No physical keyboard or firmware operation belongs to this verification.
