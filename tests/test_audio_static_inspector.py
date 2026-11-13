"""Pure synthetic PE tests; no proprietary game binary needed in CI."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "inspect_rayman2_audio.py"
SPEC = importlib.util.spec_from_file_location("inspect_rayman2_audio", SCRIPT)
assert SPEC and SPEC.loader
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def synthetic_pe32() -> bytearray:
    b = bytearray(0x500)
    b[:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    b[0x80:0x84] = b"PE\x00\x00"
    struct.pack_into("<HHI", b, 0x84, 0x14C, 1, 0x12345678)
    struct.pack_into("<H", b, 0x94, 0xE0)
    struct.pack_into("<H", b, 0x98, 0x10B)
    struct.pack_into("<I", b, 0x98 + 0x1C, 0x400000)
    b[0x178:0x180] = b".text\x00\x00\x00"
    struct.pack_into("<IIII", b, 0x180, 0x200, 0x1000, 0x200, 0x200)
    b[0x220] = 0xE8
    struct.pack_into("<i", b, 0x221, 0x401040 - 0x401025)
    struct.pack_into("<I", b, 0x230, 0xDEADBEEF)
    return b


class AudioStaticInspectorTests(unittest.TestCase):
    def test_call_resolution_and_data(self):
        pe = M.PE32(bytes(synthetic_pe32()))
        self.assertEqual(pe.call_target(0x401020), 0x401040)
        self.assertIsNone(pe.call_target(0x401021))
        self.assertEqual(pe.dword(0x401030), 0xDEADBEEF)
        self.assertEqual(pe.image_base, 0x400000)
        self.assertEqual(pe.timestamp, 0x12345678)

    def test_bad_input_is_rejected(self):
        with self.assertRaises(ValueError):
            M.PE32(b"not a pe")
        sample = synthetic_pe32()
        sample[0x84] = 0
        with self.assertRaises(ValueError):
            M.PE32(sample)

    def test_out_of_section_and_beyond_file_are_rejected(self):
        pe = M.PE32(bytes(synthetic_pe32()))
        with self.assertRaises(ValueError):
            pe.at(0x401400, 1)
        with self.assertRaises(ValueError):
            pe.read(len(pe.raw) - 1, 20)

    def test_unknown_binary_never_invents_structural_matches(self):
        with tempfile.TemporaryDirectory() as d:
            fn = Path(d) / "synthetic.exe"
            fn.write_bytes(synthetic_pe32())
            result = M.inspect(fn)
            self.assertFalse(result["known_reference_only"])
            self.assertTrue(result["not_approved_cd_proof"])
            self.assertIn("address_map_failure", result)
            self.assertNotIn("event_type11", result)


if __name__ == "__main__":
    unittest.main()
