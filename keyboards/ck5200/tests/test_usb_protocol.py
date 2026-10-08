from pathlib import Path
import importlib.util
import sys
import pytest

MODULE = Path(__file__).parents[1] / "tools" / "ck5200_usb.py"
spec = importlib.util.spec_from_file_location("ck5200_usb", MODULE)
m = importlib.util.module_from_spec(spec)
assert spec.loader
sys.modules[spec.name] = m
spec.loader.exec_module(m)


def test_v122_packet_shape():
    image = bytes((i & 0xFF) for i in range(0x48D8))
    assert m.packet_start(len(image)) == bytes.fromhex("06 a1 00 00 48 d8")
    packets = list(m.iter_data_packets(image))
    assert len(packets) == 583
    assert packets[0][0] == 0
    assert packets[0][2][:6] == bytes.fromhex("26 a2 00 00 00 00")
    assert packets[-1][0] == 0x48C0
    assert len(packets[-1][1]) == 24
    assert packets[-1][2][:6] == bytes.fromhex("1e a2 00 00 48 c0")
    assert m.packet_finish(len(image)) == bytes.fromhex("06 a3 00 00 48 d8")
    assert m.packet_reboot() == bytes.fromhex("02 a0")


def test_response_status_is_enforced():
    ok = bytes.fromhex("08 02 a1 00 00 00 6a 00")
    assert m.parse_response(ok, 0xA1) == 0x6A00
    bad = bytes.fromhex("08 02 a1 01 00 00 6a 00")
    try:
        m.parse_response(bad, 0xA1)
    except m.ProtocolError:
        pass
    else:
        raise AssertionError("non-zero status must fail")


def test_a2_next_offset():
    response = bytes.fromhex("08 02 a2 00 00 00 00 20")
    assert m.parse_response(response, 0xA2) == 0x20


def test_complete_update():
    replies = iter(bytes.fromhex(s) for s in (
        "08 02 a1 00 00 00 6a 00", "08 02 a2 00 00 00 00 20",
        "08 02 a2 00 00 00 00 21", "08 02 a3 00 00 00 00 21",
    ))
    requests = []

    class Transport:
        def transact(self, request, expect_response=True):
            requests.append(request)
            return next(replies) if expect_response else b""

    updater = m.Updater(Transport())
    assert updater.begin(33) == 0x6A00
    updater.write(bytes(range(33)))
    assert updater.finish(33) == 33
    updater.reboot()
    assert requests == [
        bytes.fromhex("06 a1 00 00 00 21"),
        bytes.fromhex("26 a2 00 00 00 00") + bytes(range(32)),
        bytes.fromhex("07 a2 00 00 00 20 20"),
        bytes.fromhex("06 a3 00 00 00 21"), bytes.fromhex("02 a0"),
    ]


def test_real_accessory_handshake_stops_before_image_data():
    requests = []

    class Out:
        def write(self, request, timeout):
            requests.append(request)
            return len(request)

    class In:
        def read(self, length, timeout):
            return bytes.fromhex("ff 55 02 00 ee 10")

    transport = m.PyUsbTransport.__new__(m.PyUsbTransport)
    transport.claimed = True
    transport.timeout_ms = 1000
    transport.ep_out, transport.ep_in = Out(), In()
    updater = m.Updater(transport)
    with pytest.raises(m.ProtocolError, match="Apple accessory handshake ff 55 02 00 ee 10"):
        updater.begin(33)
        updater.write(bytes(range(33)))
        updater.finish(33)
        updater.reboot()
    assert requests == [bytes.fromhex("06 a1 00 00 00 21")]
