#!/usr/bin/env python3
"""Loopback-only iOS Simulator bridge to the real V122 configuration runner.

Run assurance/run.py first. No physical USB or keyboard access exists here.
One HTTP server serializes commands. Reconnect preserves RAM. Power restarts it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import subprocess
from http.server import BaseHTTPRequestHandler, HTTPServer


class Machine:
    def __init__(self, runner, image, output):
        self.runner, self.image, self.output = runner, image, output
        self.child = None
        self.owner = None
        self.lose_ack = False
        self.boot = 0
        output.mkdir(parents=True, exist_ok=True)
        self.log = (output / "editor-commands.jsonl").open("a", buffering=1)
        self.power()

    def line(self, command):
        self.child.stdin.write(command + "\n")
        self.child.stdin.flush()
        if not select.select([self.child.stdout], [], [], 15)[0]:
            raise TimeoutError("V122 runner did not reply")
        reply = self.child.stdout.readline().rstrip("\n")
        if not reply:
            raise RuntimeError("V122 runner ended")
        self.log.write(json.dumps({"command": command, "reply": reply, "boot": self.boot}) + "\n")
        return reply

    def stop(self):
        if self.child is not None:
            if self.child.poll() is None:
                self.child.stdin.write("q\n")
                self.child.stdin.flush()
            self.child.wait(timeout=10)
            if self.child.returncode:
                raise RuntimeError(f"V122 runner failed: {self.child.returncode}")
            self.child = None

    def power(self):
        self.stop()
        self.boot += 1
        self.lose_ack = False
        self.child = subprocess.Popen(
            [str(self.runner), str(self.image), str(self.output / f"editor-{self.boot}.trace.jsonl"), "config-server"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1,
            env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        if self.child.stdout.readline().strip() != "ready":
            raise RuntimeError("V122 runner did not start")
        self.log.write(json.dumps({"event": "boot", "boot": self.boot,
                                   "image_sha256": hashlib.sha256(self.image.read_bytes()).hexdigest(),
                                   "runner_sha256": hashlib.sha256(self.runner.read_bytes()).hexdigest()}) + "\n")

    def command(self, owner, command):
        if not isinstance(owner, str) or not re.fullmatch(r"[A-Fa-f0-9-]{36}", owner):
            raise ValueError("Invalid owner")
        if command == "claim":
            self.owner = owner
            return "CLAIMED"
        if owner != self.owner:
            raise ValueError("This editor does not own the session")
        if command == "power":
            self.power()
            return "POWERED"
        if command == "lose-ack":
            self.lose_ack = True
            return "ARMED"
        if not isinstance(command, str) or not re.fullmatch(r"(?:p [0-9a-f]{24,72}|k [0-9a-f]{12})", command):
            raise ValueError("Only extension packets and matrix scans are allowed")
        if command.startswith("p "):
            packet = bytes.fromhex(command[2:])
            if len(packet) != packet[0] or packet[1:3] != b"\x94\x01" or packet[3] > 5:
                raise ValueError("Not an extension-only request")
        reply = self.line(command)
        if command.startswith("p ") and bytes.fromhex(command[2:])[3] == 4 and self.lose_ack:
            self.lose_ack = False
            self.log.write(json.dumps({"event": "lost-apply-ack", "actual_reply": reply}) + "\n")
            return "LOST"
        return reply


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, default=root / "build/stock_extension/assurance/runner")
    parser.add_argument("--image", type=Path, default=root / "build/stock_extension/ck5200_extension.bin")
    parser.add_argument("--output", type=Path, default=root / "build/stock_extension/editor")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    report = json.loads((args.runner.parent / "report.json").read_text())
    expected = next(case["sha256"] for case in report["cases"] if case["name"] == "extension")
    if not report["passed"] or hashlib.sha256(args.image.read_bytes()).hexdigest() != expected:
        raise RuntimeError("The image does not match passing V122 assurance evidence")
    for name, digest in {**report["build_source_sha256"], **report["source_sha256"]}.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Stale assurance source: {name}")
    machine = Machine(args.runner.resolve(), args.image.resolve(), args.output.resolve())

    def stop_requested(*_):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop_requested)

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 512:
                    raise ValueError("Invalid request size")
                data = json.loads(self.rfile.read(length))
                line = machine.command(data.get("owner"), data.get("command"))
                body = json.dumps({"line": line}).encode()
                self.send_response(200)
            except (ValueError, RuntimeError, TimeoutError, OSError) as error:
                body = json.dumps({"error": str(error)}).encode()
                self.send_response(400)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_):
            pass

    try:
        with HTTPServer(("127.0.0.1", args.port), Handler) as server:
            print(f"V122 editor bridge: http://127.0.0.1:{args.port}", flush=True)
            server.serve_forever()
    finally:
        machine.stop()


if __name__ == "__main__":
    main()
