#!/usr/bin/env bash
# Read-only USB inspection of a connected CK-5200 on macOS (M4 Mac-fallback step 1).
#
# PURPOSE
#   Capture the keyboard's USB descriptors and interface layout from the Mac
#   WITHOUT sending any write, reset, unlock, erase, or firmware command to
#   the device. Establishes which interfaces a native macOS client could use
#   for the stock update protocol, and what a future recovery investigation
#   can and cannot reach when the application firmware is not running.
#
# SAFETY
#   These commands only query the macOS IORegistry and system profiler. They
#   do not open the device, claim interfaces, or send any USB transfer.
#
# USAGE
#   bash keyboards/ck5200/tools/mac_usb_capture.sh [output-directory]
#   Plug the CK-5200 into the Mac first. Default output directory is
#   tmp/mac-usb-capture-<timestamp>/ under the repository root.
#
# EXPECTED FINDINGS (from the stock image descriptors, verified statically)
#   VID 0x352E, PID 0x2306, bcdDevice 0x0122, product "Clicks Creator Keyboard".
#   Interface 0: FF/F0/00 bulk IN+OUT (iAP2 link).
#   Interface 1: FF/F0/01 bulk IN+OUT, alternate 1 only (companion data; the
#   interface the OEM Android app claims for A1/A2/A3/A0).
#   Interface 2: HID boot keyboard, interrupt IN.
#   Interface availability with a dead application is UNKNOWN and is exactly
#   what this capture cannot answer: descriptors are served by the RUNNING
#   application. A non-enumerating device after a failed boot would present
#   either nothing or an unknown boot-stage interface.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${1:-$REPO_ROOT/tmp/mac-usb-capture-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

echo "Capturing to $OUT (read-only: IORegistry and system_profiler queries only)"

# Full USB device tree as JSON (all devices; the report filter runs offline).
system_profiler SPUSBDataType -json > "$OUT/system_profiler_usb.json" 2>&1

# Human-readable tree for quick reading.
system_profiler SPUSBDataType > "$OUT/system_profiler_usb.txt" 2>&1

# IORegistry USB plane with descriptors: read-only query, no device opens.
ioreg -p IOUSB -l -w 0 > "$OUT/ioreg_iousb_full.txt" 2>&1

# Compact device tree.
ioreg -p IOUSB -w 0 > "$OUT/ioreg_iousb_tree.txt" 2>&1

echo "--- CK-5200 sections (VID 352e / PID 2306 / product name) ---"
grep -i -B2 -A25 "352e\|2306\|Clicks" "$OUT/system_profiler_usb.txt" || echo "No CK-5200 found in system_profiler output. Is the keyboard connected and awake?"
echo
echo "Interfaces and endpoints as seen by macOS:"
grep -i -A40 "Clicks Creator Keyboard" "$OUT/system_profiler_usb.txt" || true

echo
echo "Raw captures complete. Review $OUT/system_profiler_usb.txt and $OUT/ioreg_iousb_full.txt"
echo "No USB transfer was sent to any device."
