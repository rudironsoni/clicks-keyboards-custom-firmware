# CK-5200 status

[flash-readiness.md](docs/flash-readiness.md) is the authoritative current readiness record.

Current work is diagnostics-only stock characterization on the iPhone 15 Pro Max.
Physical M1 acceptance is UNVERIFIED. R3 independent recovery is UNMET. Custom flashing is not authorized.

The experimental firmware targets CH32X035. Its keymap uses transient RAM, with no flash persistence.
Static decode and host tests do not confirm physical key labels, matrix wiring, stock layer activation, or firmware execution.

Use [the physical baseline checklist](../../ios/ClicksInspector/SIGNING_AND_BASELINE.md).
Historical port evidence remains in [X035-PORT.md](docs/X035-PORT.md); historical test counts and image sizes are not current readiness gates.
