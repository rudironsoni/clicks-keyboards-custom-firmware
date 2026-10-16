"""Offline R3 counterfactual model. No transport or hardware execution exists."""
import argparse
from dataclasses import dataclass
from enum import Enum
from hashlib import sha256
from pathlib import Path

START, END, PAGE_END, APP_END, SIZE, PAGE = 0x2000, 0x68D8, 0x6900, 0x8A00, 0xF800, 256
DIGEST = "8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8"
IMAGE = Path(__file__).resolve().parents[3] / "ios/ClicksInspector/Resources/iKeyboard_CK-5200_V122_120.bin"
CHIPS = {0x50: "R8T6", 0x51: "C8T6", 0x5E: "F8U6", 0x56: "G8U6", 0x5B: "G8R6", 0x57: "F7P6"}
# Community trace identities, not CK5200 measurements or compatibility guarantees.
TRACES = {(0x4348, 0x56, b"\x00\x02\x04\x00"),
          (0x1A86, 0x56, b"\x00\x02\x06\x00"),
          (0x1A86, 0x5E, b"\x00\x02\x06\x00")}


class Blocked(ValueError):
    pass


class Interrupted(RuntimeError):
    pass


class State(Enum):
    BLOCKED = "blocked"
    READY = "hypothetical ready"
    INTERRUPTED = "interrupted, completion unknown"
    VERIFIED = "hypothetical model verified"


def check(condition, reason):
    if not condition:
        raise Blocked(reason)


def validate_image(image):
    check(len(image) == END - START and sha256(image).hexdigest() == DIGEST,
          "Only the exact 18,648-byte V122 stock image is accepted")
    return image


def load_image():
    return validate_image(IMAGE.read_bytes())


def reply(raw, opcode, size):
    """Strict fixture framing; a framed reply alone is not operation success."""
    check(len(raw) >= 4 and raw[0] == opcode and raw[1] == 0,
          "Unknown reply opcode/header")
    check(int.from_bytes(raw[2:4], "little") == size and len(raw) == size + 4,
          "Malformed reply length")
    return raw[4:]


def compare_reply(raw):
    result = reply(raw, 0xA6, 2)
    check(result in (b"\x00\x00", b"\xf5\x00"), "Unknown comparison status")
    return result == b"\x00\x00"  # Supplied-byte compare fixture, never readback.


def a4_prefix(raw):
    """[INFERENCE] Conventional destructive prefix model, not target proof."""
    check(len(raw) == 7 and raw[:3] == b"\xa4\x04\x00", "A4 is count-only")
    count = int.from_bytes(raw[3:], "little")
    check(8 <= count <= SIZE // 1024, "Outside conservative X035 host count bounds")
    return 0, count * 1024


def decode_a5(raw):
    """Pinned protocol.rs:176-189: u32 offset, padding byte, XORed data."""
    check(len(raw) >= 8 and raw[0] == 0xA5, "Malformed A5 header")
    check(int.from_bytes(raw[1:3], "little") == len(raw) - 3, "Malformed A5 length")
    offset, data = int.from_bytes(raw[3:7], "little"), raw[8:]
    check(len(data) <= 56 and offset <= SIZE and offset + len(data) <= SIZE,
          "A5 outside conservative host chunk/code bounds")
    return offset, data  # Still encoded; this decoder grants no execution permit.


def fail_closed(method):
    def guarded(self, *args, **kwargs):
        try:
            return method(self, *args, **kwargs)
        except Blocked:
            self.state, self._next = State.BLOCKED, None
            raise
    return guarded


@dataclass(frozen=True)
class Identity:
    vid: int | None = None
    pid: int | None = None
    chip: int | None = None
    device_type: int | None = None
    loader: bytes | None = None
    capacity: int | None = None
    rdpr: int | None = None
    nrdpr: int | None = None
    write_protected: tuple | None = None  # Half-open model ranges; None is unknown.

    def identify(self, raw):
        check(self.pid == 0x55E0 and self.vid in (0x4348, 0x1A86), "Not factory USB IDs")
        check(reply(raw, 0xA1, 2) == bytes((self.chip or 0, self.device_type or 0)),
              "Identify payload mismatch; order is chip ID, device type")
        check(self.device_type == 0x23 and self.chip in CHIPS, "Not an X035 fixture")
        check((self.vid, self.chip, self.loader) in TRACES and self.capacity == SIZE,
              "Unpinned variant/version/capacity (F7P6 capacity conflict included)")
        return CHIPS[self.chip]  # RDPR does not prevent identification.


@dataclass(frozen=True)
class Boot:
    independent_entry: bool = False
    factory_loader_intact: bool = False


@dataclass(frozen=True)
class Contract:
    """Counterfactual promises only. These have no assigned wire opcodes."""
    bounded_page_erase: bool = False
    bounded_program: bool = False
    documented_compare: bool = False
    entry_survives_power_loss: bool = False
    documented_tail_read: bool = False
    tail_preserving_erase: bool = False  # Stronger than documented CH32 page erase.


@dataclass(frozen=True)
class Journal:
    image: bytes
    tail: bytes
    identity: Identity
    contract: Contract


class Lab:
    def __init__(self, identity=Identity(), boot=Boot(), contract=Contract(), unreadable=()):
        self.identity, self.boot, self.contract = identity, boot, contract
        self.unreadable = unreadable
        # Private device truth for the oracle, NOT host-readable bytes or a backup.
        self._flash = bytearray((i * 17 + 37) % 256 for i in range(SIZE))
        self._flash[START:APP_END] = bytes(APP_END - START)  # Entire app invalid.
        self._separate = {"system_flash": bytes([0x31]) * 3328,
                          "manufacturer": bytes([0x52]) * 256,
                          "options": bytes([0x73]) * 256,
                          "esig": bytes(range(12)), "external_auth": b"opaque fixture"}
        self._original = bytes(self._flash)
        self._original_separate = self._separate.copy()
        self.journal = self._evidence = self._next = self._fault = None
        self.state = State.BLOCKED

    @property
    def tail_backup(self):
        return self.journal.tail if self.journal else None

    @fail_closed
    def enter(self):
        check(self.boot.independent_entry and self.boot.factory_loader_intact,
              "Independent factory entry/intact loader unproven; app cannot help")
        i = self.identity
        check(i.chip is not None and i.device_type is not None, "Unknown identity")
        return i.identify(bytes((0xA1, 0, 2, 0, i.chip, i.device_type)))

    @fail_closed
    def gate(self):
        self.enter()
        i, c = self.identity, self.contract
        check(i.rdpr == 0xA5 and i.nrdpr == 0x5A, "Read protection enabled/unknown/invalid")
        check(i.write_protected is not None, "Unknown write protection")
        check(not any(a < PAGE_END and b > START for a, b in i.write_protected),
              "Application erase span is write-protected")
        check(c.bounded_page_erase and c.bounded_program and c.documented_compare,
              "Selective erase/program/compare contract unproven")
        check(c.entry_survives_power_loss, "Independent interruption/resume guarantee missing")
        check(c.tail_preserving_erase, "Strict tail-preserving erase contract missing")
        check(c.documented_tail_read, "40-byte tail unknown; no read contract")

    @fail_closed
    def prepare(self, image, fault=None):
        validate_image(image)
        self.gate()
        check(fault is None or (fault[0] in ("erase", "program", "verify", "lost-transfer")
              and fault[1] in range(START, PAGE_END, PAGE)), "Unknown fault fixture")
        check(self.state != State.READY, "Transaction already active")
        check(self._evidence is None or self.journal is self._evidence,
              "Original transaction journal lost; restore evidence before retry")
        if self.journal is None:
            check(not any(a < PAGE_END and b > END for a, b in self.unreadable),
                  "40-byte tail unreadable")
            self.journal = Journal(bytes(image), bytes(self._flash[END:PAGE_END]),
                                   self.identity, self.contract)
            self._evidence = self.journal  # Simulator provenance, not device-stored journal.
        check(self.journal.identity == self.identity and self.journal.contract == self.contract,
              "Transaction identity/contract changed")
        self._next, self._fault = ("erase", START), fault
        self.state = State.READY

    def _authorize(self, operation, address):
        self.gate()
        check(self.state == State.READY and self._next == (operation, address)
              and self.journal is not None and self.journal is self._evidence,
              "No active permit for this exact next operation")
        check(self.journal.identity == self.identity and self.journal.contract == self.contract,
              "Transaction identity/contract changed")

    def _interrupt(self, stage, address):
        if self._fault == (stage, address):
            self.state, self._next = State.INTERRUPTED, None
            self.check_preservation()
            raise Interrupted(f"{stage} at {address:#x}; no completion claim")

    @fail_closed
    def erase_page(self, address):
        self._authorize("erase", address)
        # Counterfactual no-collateral operation: excluded tail NEVER changes.
        length = min(PAGE, END - address)
        if self._fault == ("erase", address):
            length = (length // 8) * 4
        self._flash[address:address + length] = b"\xff" * length
        self._interrupt("erase", address)
        self._interrupt("lost-transfer", address)
        self._next = ("program", address)

    @fail_closed
    def program(self, address, data):
        self._authorize("program", address)
        expected = self.journal.image[address - START:address - START + PAGE]
        check(data == expected and len(data) % 4 == 0, "Not exact transaction image data")
        old = self._flash[address:address + len(data)]
        check(all(a & b == b for a, b in zip(old, data)), "Programming cannot set zero bits to one")
        if self._fault == ("program", address):
            data = data[:(len(data) // 8) * 4]
        self._flash[address:address + len(data)] = data
        self._interrupt("program", address)
        self._next = ("compare", address)

    @fail_closed
    def check_preservation(self, final=False):
        # Strict at EVERY checkpoint, including interruption. Not factory readback.
        check(self._flash[:START] == self._original[:START]
              and self._flash[END:] == self._original[END:]
              and self._separate == self._original_separate, "Model preservation failure")

    @fail_closed
    def compare_page(self, address):
        self._authorize("compare", address)
        self._interrupt("verify", address)
        end = min(address + PAGE, END)
        check(self._flash[address:end] == self.journal.image[address - START:end - START],
              "Hypothetical page comparison failed")
        self.check_preservation()
        self._next = ("erase", end) if end < END else ("verify", END)

    @fail_closed
    def verify(self, image):
        self._authorize("verify", END)
        validate_image(image)
        check(bytes(self._flash[START:END]) == image
              and sha256(self._flash[START:END]).hexdigest() == DIGEST, "Model compare failed")
        self.check_preservation(final=True)
        self.state, self._next = State.VERIFIED, None

    @fail_closed
    def restore(self, image, fault=None):
        """fault=(erase|program|verify|lost-transfer, page address), model only."""
        self.prepare(image, fault)
        for address in range(START, PAGE_END, PAGE):
            self.erase_page(address)
            self.program(address, self.journal.image[address - START:address - START + PAGE])
            self.compare_page(address)
        self.verify(image)

    def host_restart(self):
        """Lose ALL host state. Retain device truth and simulator-only provenance."""
        fresh = Lab(self.identity, self.boot, self.contract, self.unreadable)
        for name in ("_flash", "_separate", "_original", "_original_separate", "_evidence"):
            setattr(fresh, name, getattr(self, name))
        self.state, self._next = State.BLOCKED, None
        self.journal = None
        return fresh

    @fail_closed
    def restore_journal(self, journal):
        check(journal is not None and journal is self._evidence, "Invalid original journal evidence")
        self.journal = journal  # In-memory fixture only; no durable host storage exists.

    @fail_closed
    def factory_command(self, operation):
        if isinstance(operation, bytes) and operation[:1] == b"\xa5":
            address, data = decode_a5(operation)
            check(START <= address and address + len(data) <= END, "A5 intersects excluded bytes")
        raise Blocked("Factory execution unsupported; fixtures only")


def hypothetical_fixture():
    # Explicit counterfactual selection, NOT measured keyboard properties.
    return Lab(Identity(0x1A86, 0x55E0, 0x5E, 0x23, b"\x00\x02\x06\x00",
                        SIZE, 0xA5, 0x5A, ((0, START), (0xF600, SIZE))),
               Boot(True, True), Contract(True, True, True, True, True, True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hypothetical", action="store_true", help="in-memory counterfactual only")
    args = parser.parse_args()
    image = load_image()
    model = hypothetical_fixture() if args.hypothetical else Lab()
    print("OFFLINE ONLY: no device access. Stock V122 length and SHA-256 matched.")
    print("A4 count 20: modeled prefix [0, 0x5000), destructive; execution denied.")
    try:
        model.restore(image)
    except Blocked as error:
        print(f"BLOCKED: {error}. Selective erase, protection and interruption remain unproven.")
        return 2
    print("HYPOTHETICAL ONLY: image digest and strict region oracle match; no OEM execution proof.")
    print("Assumed: tail NEVER changes, exact transaction, readable tail, bounded program/compare,")
    print("independent loader after power loss, original journal retained. No durable journal exists.")
    print("Real CK5200 restoration remains BLOCKED. This is not hardware evidence.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
