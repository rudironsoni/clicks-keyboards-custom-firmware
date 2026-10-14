# CK-5200 status

[flash-readiness.md](docs/flash-readiness.md) is the authoritative current readiness record.

The stock diagnostics baseline on the iPhone 15 Pro Max is complete and accepted as of 2026-10-14.
Comprehensive physical characterization remains UNVERIFIED beyond the recorded observations.
R3 independent recovery is UNMET. Custom flashing is not authorized.

The experimental firmware targets CH32X035. Its keymap uses transient RAM, with no flash persistence.
Static decode and host tests do not confirm physical key labels, matrix wiring, stock layer activation, or firmware execution.

See [the baseline acceptance record](../../ios/ClicksInspector/SIGNING_AND_BASELINE.md#acceptance-record). Further recording is not required for baseline closure.
Historical port evidence remains in [X035-PORT.md](docs/X035-PORT.md); historical test counts and image sizes are not current readiness gates.
