"""Software-only fixtures. Internal oracle inspection is not hardware readback."""
from dataclasses import replace
import unittest

import lab


class RecoveryLabTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = lab.load_image()

    def test_stock_and_bad_images(self):
        self.assertEqual(len(self.image), 18648)
        for image in (b"", self.image[:-1], self.image + b"\xff",
                      bytes([self.image[0] ^ 1]) + self.image[1:]):
            with self.subTest(length=len(image)):
                model = lab.hypothetical_fixture()
                with self.assertRaises(lab.Blocked):
                    model.restore(image)
                self.assertEqual(model._flash, model._original)

    def test_identity_and_independent_boot(self):
        for vid, chip, loader in lab.TRACES:
            model = lab.hypothetical_fixture()
            model.identity = replace(model.identity, vid=vid, chip=chip, loader=loader, rdpr=0xFF)
            self.assertEqual(model.enter(), lab.CHIPS[chip])
        changes = ({"vid": 0x1234}, {"pid": 1}, {"chip": 0x5A}, {"chip": 0x57},
                   {"device_type": 0x22}, {"loader": b"unknown"}, {"capacity": 65536})
        for change in changes:
            with self.subTest(change=change):
                model = lab.hypothetical_fixture()
                model.identity = replace(model.identity, **change)
                with self.assertRaises(lab.Blocked):
                    model.restore(self.image)
        for boot in (lab.Boot(), lab.Boot(True, False), lab.Boot(False, True)):
            model = lab.hypothetical_fixture()
            model.boot = boot
            with self.assertRaises(lab.Blocked):
                model.enter()
        with self.assertRaises(lab.Blocked):
            lab.Lab().restore(self.image)

    def test_protocol_fixtures_and_rejected_execution(self):
        self.assertEqual(lab.a4_prefix(bytes.fromhex("a4 04 00 14 00 00 00")), (0, 0x5000))
        self.assertEqual(((len(self.image) + 1023) // 1024) * 1024, 0x4C00)
        for raw in ("a4 04 00 01 00 00 00", "a4 04 00 3f 00 00 00", "a4 08 00 00 20 00 00"):
            with self.assertRaises(lab.Blocked):
                lab.a4_prefix(bytes.fromhex(raw))
        self.assertFalse(lab.compare_reply(bytes.fromhex("a6 00 02 00 f5 00")))
        self.assertTrue(lab.compare_reply(bytes.fromhex("a6 00 02 00 00 00")))
        for raw in ("a5 00 02 00 00 00", "a6 01 02 00 00 00", "a6 00 02 00 00",
                    "a6 00 01 00 00", "a6 00 02 00 00 01", "a6 00 02 00 00 00 ff"):
            with self.assertRaises(lab.Blocked):
                lab.compare_reply(bytes.fromhex(raw))
        for command in (0xA2, 0xA4, 0xA5, 0xA8, 0xA9, 0xAA, 0xAB, 0xC3, 0xC4,
                        0xC5, 0xFF, "mass erase", "unlock", "option write", "RAM helper"):
            with self.assertRaises(lab.Blocked):
                lab.hypothetical_fixture().factory_command(command)
        # Actual source layout: opcode, u16 length, u32 offset, one padding byte, data.
        for offset, length, reason in ((0, 9, "excluded"), (lab.START, 8, "length"),
                                       (lab.SIZE, 9, "bounds"), (lab.START, 9, "unsupported")):
            packet = b"\xa5" + length.to_bytes(2, "little") + offset.to_bytes(4, "little") + b"\x37ABCD"
            model = lab.hypothetical_fixture()
            model.prepare(self.image)
            if length == 9 and offset < lab.SIZE:
                self.assertEqual(lab.decode_a5(packet), (offset, b"ABCD"))
            with self.assertRaisesRegex(lab.Blocked, reason):
                model.factory_command(packet)
            self.assertEqual(model._flash, model._original)
            self.assertEqual(model.state, lab.State.BLOCKED)
        model = lab.hypothetical_fixture()
        with self.assertRaises(lab.Blocked):
            model.identity.identify(bytes.fromhex("a1 00 02 00 23 5e"))

    def test_gates_and_tail_unknown_or_unreadable(self):
        for change in ({"rdpr": None}, {"rdpr": 0xFF}, {"nrdpr": None}, {"nrdpr": 0},
                       {"write_protected": None}, {"write_protected": ((0x6800, 0x7000),)}):
            model = lab.hypothetical_fixture()
            model.identity = replace(model.identity, **change)
            with self.subTest(change=change), self.assertRaises(lab.Blocked):
                model.restore(self.image)
            self.assertEqual(model._flash, model._original)
        for field in lab.Contract.__dataclass_fields__:
            model = lab.hypothetical_fixture()
            model.contract = replace(model.contract, **{field: False})
            with self.subTest(capability=field), self.assertRaises(lab.Blocked):
                model.restore(self.image)
            self.assertIsNone(model.tail_backup)
            self.assertEqual(model._flash, model._original)
        model = lab.hypothetical_fixture()
        model.unreadable = ((lab.END, lab.PAGE_END),)
        with self.assertRaises(lab.Blocked):
            model.restore(self.image)
        self.assertIsNone(model.tail_backup)

    def test_boundaries_alignment_and_monotonic_bits(self):
        for address in (-256, 0, 0x1F00, 0x2001, 0x6900, 0x8900, 0xF600, 0xF700,
                        0xF800, 0x08002000, 0x1FFF0000):
            model = lab.hypothetical_fixture()
            model.prepare(self.image)
            with self.subTest(address=address), self.assertRaises(lab.Blocked):
                model.erase_page(address)
        for address, data in ((0x2001, b"1234"), (0x20FC, b"12345678"),
                              (0x2000, b"123"), (0x2000, b""), (0x6900, b"1234")):
            model = lab.hypothetical_fixture()
            model.prepare(self.image)
            model.erase_page(lab.START)
            with self.assertRaises(lab.Blocked):
                model.program(address, data)
        model = lab.hypothetical_fixture()
        model.prepare(self.image)
        model.erase_page(lab.START)
        model._flash[lab.START:lab.START + lab.PAGE] = bytes(lab.PAGE)
        with self.assertRaisesRegex(lab.Blocked, "zero bits"):
            model.program(lab.START, self.image[:lab.PAGE])
        model.check_preservation()

    def test_exact_transaction_and_stale_operations(self):
        for state in (None, lab.State.READY, lab.State.BLOCKED, lab.State.INTERRUPTED, lab.State.VERIFIED):
            for operation in ("erase", "tail", "wrong data", "replay"):
                model = lab.hypothetical_fixture()
                if state is not None:
                    model.prepare(self.image)
                    model.state = state
                if state == lab.State.READY and operation in ("wrong data", "replay"):
                    model.erase_page(lab.START)
                before = bytes(model._flash)
                with self.subTest(state=state, operation=operation), self.assertRaises(lab.Blocked):
                    if operation in ("erase", "replay"):
                        model.erase_page(lab.START + lab.PAGE if operation == "erase" else lab.START)
                    else:
                        model.program(lab.END if operation == "tail" else lab.START, b"\x55" * 40)
                self.assertEqual(model._flash, before)
                self.assertEqual(model.state, lab.State.BLOCKED)
                with self.assertRaises(lab.Blocked):
                    model.erase_page(lab.START)

    def test_strict_tail_and_ordinary_erase_counterexample(self):
        model = lab.hypothetical_fixture()
        ordinary = bytearray(model._flash)
        ordinary[lab.PAGE_END - lab.PAGE:lab.PAGE_END] = b"\xff" * lab.PAGE
        self.assertNotEqual(ordinary[lab.END:lab.PAGE_END], model._original[lab.END:lab.PAGE_END])
        model.contract = replace(model.contract, tail_preserving_erase=False)
        with self.assertRaisesRegex(lab.Blocked, "tail-preserving"):
            model.restore(self.image)
        self.assertEqual(model._flash, model._original)

    def test_success_and_oracle_failures(self):
        model = lab.hypothetical_fixture()
        self.assertEqual(model._flash[lab.START:lab.APP_END], bytes(lab.APP_END - lab.START))
        model.unreadable = ((0, lab.START), (lab.PAGE_END, lab.SIZE))
        model.restore(self.image)
        self.assertEqual(model.state, lab.State.VERIFIED)
        self.assertEqual(len(model.tail_backup), 40)
        model.check_preservation(final=True)
        for region in ("image", "oem", "system_flash"):
            damaged = lab.hypothetical_fixture()
            damaged.prepare(self.image)
            for address in range(lab.START, lab.PAGE_END, lab.PAGE):
                damaged.erase_page(address)
                damaged.program(address, self.image[address - lab.START:address - lab.START + lab.PAGE])
                damaged.compare_page(address)
            if region == "system_flash":
                damaged._separate[region] = b"damaged"
            else:
                damaged._flash[lab.START if region == "image" else 0xF700] ^= 1
            with self.assertRaisesRegex(lab.Blocked, "compare failed|preservation failure"):
                damaged.verify(self.image)
            self.assertEqual(damaged.state, lab.State.BLOCKED)

    def test_host_restart_requires_original_journal(self):
        model = lab.hypothetical_fixture()
        with self.assertRaises(lab.Interrupted):
            model.restore(self.image, ("erase", lab.PAGE_END - lab.PAGE))
        journal = model.journal
        model = model.host_restart()
        self.assertEqual(model.state, lab.State.BLOCKED)
        self.assertIsNone(model.tail_backup)
        interrupted = bytes(model._flash)
        with self.assertRaisesRegex(lab.Blocked, "journal lost"):
            model.restore(self.image)
        with self.assertRaises(lab.Blocked):
            model.restore_journal(replace(journal, tail=b"\x55" * 40))
        self.assertEqual(model._flash, interrupted)
        model.restore_journal(journal)
        model.restore(self.image)
        model.check_preservation(final=True)

    def test_partial_power_loss_lost_transfer_and_retry(self):
        for stage in ("erase", "program", "verify", "lost-transfer"):
            for address in (lab.START, lab.PAGE_END - lab.PAGE):
                with self.subTest(stage=stage, address=address):
                    model = lab.hypothetical_fixture()
                    with self.assertRaises(lab.Interrupted):
                        model.restore(self.image, (stage, address))
                    self.assertEqual(model.state, lab.State.INTERRUPTED)
                    model.check_preservation()
                    backup = model.tail_backup
                    model.contract = replace(model.contract, entry_survives_power_loss=False)
                    interrupted = bytes(model._flash)
                    with self.assertRaises(lab.Blocked):
                        model.restore(self.image)
                    self.assertEqual(model._flash, interrupted)
                    model.contract = replace(model.contract, entry_survives_power_loss=True)
                    model.restore(self.image)
                    self.assertEqual(model.tail_backup, backup)
                    model.check_preservation(final=True)
                    self.assertEqual(model.state, lab.State.VERIFIED)


if __name__ == "__main__":
    unittest.main()
