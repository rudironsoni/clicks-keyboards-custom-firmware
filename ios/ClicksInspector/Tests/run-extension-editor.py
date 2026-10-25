#!/usr/bin/env python3
"""Run the editor workflow against genuine V122, optionally with Simulator UI.

First run keyboards/ck5200/stock_extension/assurance/run.py.
This script never opens a physical accessory or USB device.
"""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[3]
    app = root / "ios/ClicksInspector"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="Simulator UDID. Omit to run only the host workflow.")
    parser.add_argument("--configuration", choices=["Debug", "ExtensionInstaller"], default="Debug")
    parser.add_argument("--output", type=Path, default=app / "build/extension-editor")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bridge = root / "keyboards/ck5200/stock_extension/assurance/editor_server.py"
    server = subprocess.Popen([sys.executable, str(bridge), "--output", str(output / "emulator")],
                              stdout=subprocess.PIPE, text=True)
    try:
        if not server.stdout.readline().startswith("V122 editor bridge:"):
            raise RuntimeError("V122 editor bridge failed to start. Port 8765 must be free.")
        sources = ["StockExtensionProtocol", "StockExtensionTransport", "CustomKeyboardProtocol",
                   "StockExtensionProfile", "StockExtensionController", "V122EmulatorLink",
                   "KeyboardPresentation", "KeycodeNames", "FactoryLayout"]
        executable = output / "editor-test"
        subprocess.run(["xcrun", "swiftc", *(str(app / f"Sources/{name}.swift") for name in sources),
                        str(app / "Tests/StockExtensionEditorTests.swift"), "-o", str(executable)], check=True)
        with (output / "host-workflow.log").open("w") as log:
            subprocess.run([str(executable), "http://127.0.0.1:8765"], stdout=log, stderr=subprocess.STDOUT, check=True)
        if args.device:
            subprocess.run(["xcodegen", "generate", "--spec", str(app / "project.yml")], check=True)
            with (output / "ui-workflow.log").open("w") as log:
                result = subprocess.run(["xcodebuild", "-project", str(app / "ClicksInspector.xcodeproj"),
                                "-scheme", "StockExtensionEditor", "-configuration", args.configuration,
                                "-destination", f"platform=iOS Simulator,id={args.device}",
                                "-only-testing:ClicksInspectorUITests/StockExtensionUITests",
                                "-only-testing:ClicksInspectorAppTests/StockReceiverTests",
                                "-parallel-testing-enabled", "NO", "-resultBundlePath", str(output / "ui.xcresult"),
                                "CODE_SIGNING_ALLOWED=NO", "test"], stdout=log, stderr=subprocess.STDOUT)
            subprocess.run(["xcrun", "xcresulttool", "export", "attachments", "--path", str(output / "ui.xcresult"),
                            "--output-path", str(output / "screenshots")], check=True)
            result.check_returncode()
        print(f"PASS extension editor workflow. Evidence: {output}")
    finally:
        server.terminate()
        server.wait(timeout=20)
        if server.returncode != 0:
            raise RuntimeError(f"V122 editor bridge failed: {server.returncode}")


if __name__ == "__main__":
    main()
