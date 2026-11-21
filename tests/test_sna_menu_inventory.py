"""Synthetic tests only: commercial Rayman 2 files are not committed."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_rayman2_sna import (
    SnaFormatError, decode_sna, inspect_sna, inventory_blocks
)

MAGIC = bytes.fromhex("79ccb56a")


def encode(decoded: bytes) -> bytes:
    # Rayman 2 masking is symmetric XOR. Independent fixture encryption uses
    # the same advancing mask as decoding and never embeds game assets.
    return decode_sna(decoded)


class MenuSnaInventoryTests(unittest.TestCase):
    def test_synthetic_sna_roundtrip_and_names(self):
        data = b"YAM_GenMenu_I1\0YAM_CurseurMenu_I1\0RandomGameObject\0"
        header = struct.pack(
            "<BBBiIIII", 4, 1, 2, 0x07E70020, 0, 0, 0, len(data)
        )
        decoded = MAGIC + header + data
        encrypted = encode(decoded)
        self.assertNotIn(b"YAM_GenMenu_I1", encrypted)
        self.assertEqual(decode_sna(encrypted), decoded)
        report = inspect_sna(encrypted)
        self.assertEqual(report["block_count"], 1)
        self.assertEqual(report["nonempty_block_count"], 1)
        self.assertEqual(
            [x["name"] for x in report["menu_related_name_candidates"]],
            ["YAM_GenMenu_I1", "YAM_CurseurMenu_I1"],
        )
        self.assertFalse(report["resolution_submenu_identified"])

    def test_empty_block_and_nonzero_block(self):
        prefix = struct.pack("<BBBi", 10, 0, 0, -1)
        payload = b"ABC\0"
        full = struct.pack("<BBBiIIII", 13, 1, 2, 77, 0, 0, 0, len(payload))
        blocks = inventory_blocks(MAGIC + prefix + full + payload)
        self.assertEqual(len(blocks), 2)
        self.assertEqual(blocks[0]["data_size"], 0)
        self.assertEqual(blocks[1]["data_size"], len(payload))

    def test_incorrect_magic_and_truncation_rejected(self):
        for raw in [b"", b"SNA!abcd", b"\0" * 128]:
            with self.assertRaises(SnaFormatError):
                decode_sna(raw)
        with self.assertRaises(SnaFormatError):
            inventory_blocks(MAGIC + b"\x01")

    def test_declared_size_exceeds_file_rejected(self):
        hdr = struct.pack("<BBBiIIII", 4, 1, 2, 123, 0, 0, 0, 50000)
        with self.assertRaises(SnaFormatError):
            inventory_blocks(MAGIC + hdr + b"123")
        with self.assertRaises(SnaFormatError):
            inventory_blocks(MAGIC + hdr[:13])


if __name__ == "__main__":
    unittest.main()
