#!/usr/bin/env python3
"""CK-5200 inspection and experimental update protocol tools.

Endpoint presence does not prove stock update-session compatibility.
`inspect` reads descriptors without configuring or claiming an interface.
`packets` does not open USB. See docs/RECOVERY.md before any device write.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import time
from dataclasses import dataclass
from typing import Iterable, Optional, Protocol, Sequence

VID = 0x352E
PID = 0x2306
EP_OUT = 0x02
EP_IN = 0x82
USB_TIMEOUT_MS = 1000
MAX_IMAGE = 0x6A00
CHUNK_SIZE = 32

KNOWN_STOCK = {
    "1.2.1": "1c9ae5b2a4a838a5c62db0ce38c6d1d71c91fcc4101f0f8e37b365a746dac4aa",
    "1.2.2": "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8",
}

class ProtocolError(RuntimeError):
    pass

class Transport(Protocol):
    def transact(self, request: bytes, expect_response: bool = True) -> bytes: ...
    def close(self) -> None: ...

def u32be(value: int) -> bytes:
    return struct.pack(">I", value)

def read_u32be(data: bytes) -> int:
    if len(data) != 4:
        raise ValueError("expected exactly four bytes")
    return struct.unpack(">I", data)[0]

def packet_start(size: int) -> bytes:
    return bytes((0x06, 0xA1)) + u32be(size)

def packet_data(offset: int, chunk: bytes) -> bytes:
    if not 1 <= len(chunk) <= CHUNK_SIZE:
        raise ValueError("A2 chunks must contain 1..32 bytes")
    return bytes((len(chunk) + 6, 0xA2)) + u32be(offset) + chunk

def packet_finish(size: int) -> bytes:
    return bytes((0x06, 0xA3)) + u32be(size)

def packet_reboot() -> bytes:
    return b"\x02\xA0"

def iter_data_packets(image: bytes) -> Iterable[tuple[int, bytes, bytes]]:
    for offset in range(0, len(image), CHUNK_SIZE):
        chunk = image[offset : offset + CHUNK_SIZE]
        yield offset, chunk, packet_data(offset, chunk)

def parse_response(response: Sequence[int], expected_cmd: int) -> int:
    data = bytes(response)
    if len(data) < 8:
        raise ProtocolError(
            f"short response: expected >=8 bytes, got {len(data)}, response={data.hex(' ')}"
        )
    if data[0] != 0x08 or data[1] != 0x02:
        raise ProtocolError(f"bad response framing: {data[:8].hex(' ')}")
    if data[2] != expected_cmd:
        raise ProtocolError(
            f"response command mismatch: expected A{expected_cmd & 0x0F:X}, got 0x{data[2]:02x}"
        )
    if data[3] != 0x00:
        raise ProtocolError(
            f"device rejected command 0x{expected_cmd:02x}, status=0x{data[3]:02x}, "
            f"response={data[:8].hex(' ')}"
        )
    return read_u32be(data[4:8])

@dataclass(frozen=True)
class ImageInfo:
    path: pathlib.Path
    size: int
    sha256: str
    stock_version: Optional[str]

def inspect_image(path: pathlib.Path) -> tuple[ImageInfo, bytes]:
    image = path.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    stock = next((v for v, h in KNOWN_STOCK.items() if h == digest), None)
    return ImageInfo(path, len(image), digest, stock), image

class PyUsbTransport:
    def __init__(self, timeout_ms: int = USB_TIMEOUT_MS, *, claim: bool = True):
        try:
            import usb.core  # type: ignore
            import usb.util  # type: ignore
        except ImportError as exc:
            raise RuntimeError("PyUSB is required. Install with: python3 -m pip install pyusb") from exc

        self.usb_core = usb.core
        self.usb_util = usb.util
        self.timeout_ms = timeout_ms
        self.claimed = False
        self.device = usb.core.find(idVendor=VID, idProduct=PID)
        if self.device is None:
            raise RuntimeError(f"CK-5200 USB device {VID:04x}:{PID:04x} not found")

        config = self.device.get_active_configuration()
        selected = None
        for interface in config:
            endpoints = {int(ep.bEndpointAddress): ep for ep in interface}
            if (EP_OUT in endpoints and EP_IN in endpoints
                    and all((int(endpoints[e].bmAttributes) & 3) == 2 for e in (EP_OUT, EP_IN))):
                selected = (interface, endpoints[EP_OUT], endpoints[EP_IN])
                break
        if selected is None:
            raise RuntimeError(
                f"device found, but no interface exposes endpoints 0x{EP_OUT:02x}/0x{EP_IN:02x}"
            )

        self.interface, self.ep_out, self.ep_in = selected
        self.interface_number = int(self.interface.bInterfaceNumber)
        if claim:
            try:
                self.usb_util.claim_interface(self.device, self.interface_number)
                self.claimed = True
            except Exception:
                self.usb_util.dispose_resources(self.device)
                raise

    def transact(self, request: bytes, expect_response: bool = True) -> bytes:
        if not self.claimed:
            raise ProtocolError("USB writes require an explicitly claimed update session")
        print(f"USB OUT 0x{EP_OUT:02x}: {request.hex(' ')}", file=sys.stderr)
        written = self.ep_out.write(request, timeout=self.timeout_ms)
        if written != len(request):
            raise ProtocolError(f"short USB write: wrote {written}/{len(request)} bytes")
        if not expect_response:
            return b""
        response = bytes(self.ep_in.read(64, timeout=self.timeout_ms))
        print(f"USB IN 0x{EP_IN:02x}: {response.hex(' ')}", file=sys.stderr)
        if response == bytes.fromhex("ff 55 02 00 ee 10"):
            raise ProtocolError(
                "received Apple accessory handshake ff 55 02 00 ee 10, not an update reply; "
                "stock session setup is unverified, stopping without retry"
            )
        return response

    def close(self) -> None:
        try:
            if self.claimed:
                self.usb_util.release_interface(self.device, self.interface_number)
                self.claimed = False
        finally:
            self.usb_util.dispose_resources(self.device)

class Updater:
    def __init__(self, transport: Transport):
        self.t = transport

    def begin(self, size: int) -> int:
        if not 0 < size <= MAX_IMAGE:
            raise ValueError(f"image size must be 1..0x{MAX_IMAGE:x}, got 0x{size:x}")
        maximum = parse_response(self.t.transact(packet_start(size)), 0xA1)
        if maximum < size:
            raise ProtocolError(f"device reports max 0x{maximum:x}, image is 0x{size:x}")
        return maximum

    def write(self, image: bytes, progress=None) -> None:
        accepted = 0
        for i, (offset, chunk, request) in enumerate(iter_data_packets(image), start=1):
            if offset != accepted:
                raise ProtocolError(f"local offset desync: local=0x{offset:x}, device=0x{accepted:x}")
            accepted = parse_response(self.t.transact(request), 0xA2)
            expected = offset + len(chunk)
            if accepted != expected:
                raise ProtocolError(
                    f"device accepted offset 0x{accepted:x}, expected 0x{expected:x} after packet {i}"
                )
            if progress:
                progress(accepted, len(image))

    def finish(self, size: int) -> int:
        committed = parse_response(self.t.transact(packet_finish(size)), 0xA3)
        if committed != size:
            raise ProtocolError(f"finish size mismatch: device=0x{committed:x}, host=0x{size:x}")
        return committed

    def reboot(self) -> None:
        self.t.transact(packet_reboot(), expect_response=False)

def cmd_packets(args: argparse.Namespace) -> int:
    info, image = inspect_image(pathlib.Path(args.image))
    packets = [packet_start(len(image))]
    packets.extend(req for _, _, req in iter_data_packets(image))
    packets.append(packet_finish(len(image)))
    packets.append(packet_reboot())
    output = {
        "path": str(info.path),
        "size": info.size,
        "size_hex": f"0x{info.size:x}",
        "sha256": info.sha256,
        "known_stock_version": info.stock_version,
        "a2_packets": (len(image) + CHUNK_SIZE - 1) // CHUNK_SIZE,
        "start": packets[0].hex(" "),
        "first_data": packets[1].hex(" ") if len(image) else None,
        "last_data": packets[-3].hex(" ") if len(image) else None,
        "finish": packets[-2].hex(" "),
        "reboot": packets[-1].hex(" "),
    }
    print(json.dumps(output, indent=2))
    return 0

def cmd_inspect(args: argparse.Namespace) -> int:
    t = PyUsbTransport(claim=args.query_version)
    try:
        d = t.device
        print(f"device: {VID:04x}:{PID:04x}")
        print(f"interface: {t.interface_number}")
        print(f"bulk OUT: 0x{int(t.ep_out.bEndpointAddress):02x}")
        print(f"bulk IN:  0x{int(t.ep_in.bEndpointAddress):02x}")
        print(f"bcdDevice: 0x{int(d.bcdDevice):04x}")
        print("Descriptor read complete. Update session and recovery are not verified.")
        if args.query_version:
            version = parse_response(t.transact(b"\x02\x03"), 0x03)
            print(f"version payload: 0x{version:08x}")
    finally:
        t.close()
    return 0

def cmd_flash(args: argparse.Namespace) -> int:
    info, image = inspect_image(pathlib.Path(args.image))
    if not 0 < info.size <= MAX_IMAGE:
        raise SystemExit(f"image size must be 1..0x{MAX_IMAGE:x}, got 0x{info.size:x}")
    if args.confirm != "CK-5200":
        raise SystemExit("refusing write: pass --confirm CK-5200")
    if info.stock_version is None and not args.allow_unknown_image:
        raise SystemExit(
            "refusing unknown/custom image: pass --allow-unknown-image only after you have reviewed the image"
        )

    kind = f"stock {info.stock_version}" if info.stock_version else "UNKNOWN/CUSTOM"
    print(f"image: {info.path}")
    print(f"size:  {info.size} (0x{info.size:x})")
    print(f"sha256: {info.sha256}")
    print(f"class: {kind}")
    if info.stock_version is None:
        from validate_image import validate_binary
        validate_binary(image)
        print("WARNING: post-reset installer acceptance of arbitrary images is still unverified.", file=sys.stderr)

    t = PyUsbTransport(timeout_ms=args.timeout)
    updater = Updater(t)
    try:
        if int(t.device.bcdDevice) != 0x9001:
            raise ProtocolError(
                "stock iPhone USB update session is not implemented; "
                "refusing A1/A2/A3/A0. See docs/USB_AND_IPHONE.md"
            )
        maximum = updater.begin(info.size)
        print(f"A1 accepted, device maximum 0x{maximum:x}")

        last_print = -1
        def progress(done: int, total: int) -> None:
            nonlocal last_print
            pct = done * 100 // total
            if pct != last_print and (pct % 5 == 0 or done == total):
                print(f"A2 {done:5d}/{total} bytes ({pct:3d}%)")
                last_print = pct

        updater.write(image, progress=progress)
        committed = updater.finish(info.size)
        print(f"A3 committed staged length 0x{committed:x}")
        if args.no_reboot:
            print("NOT rebooting. The staged image may still be installed on a later reset.")
        else:
            print("A0 reboot")
            updater.reboot()
            time.sleep(0.25)
    finally:
        t.close()
    return 0

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Clicks CK-5200 stock USB updater protocol client")
    sub = p.add_subparsers(dest="command", required=True)

    inspect = sub.add_parser("inspect", help="read CK-5200 descriptors without changing USB configuration")
    inspect.add_argument("--query-version", action="store_true",
                         help="also send the stock read-only 02 03 query; never stage or reboot")
    inspect.set_defaults(func=cmd_inspect)

    packets = sub.add_parser("packets", help="show the exact packet plan for an image; no USB access")
    packets.add_argument("image")
    packets.set_defaults(func=cmd_packets)

    flash = sub.add_parser("flash", help="stage an image with A1/A2/A3 and optionally reboot with A0")
    flash.add_argument("image")
    flash.add_argument("--confirm", required=True, metavar="CK-5200")
    flash.add_argument("--allow-unknown-image", action="store_true")
    flash.add_argument("--no-reboot", action="store_true")
    flash.add_argument("--timeout", type=int, default=USB_TIMEOUT_MS)
    flash.set_defaults(func=cmd_flash)
    return p

def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
