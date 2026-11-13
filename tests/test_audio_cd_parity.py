"""Pure synthetic checks for the approved-CD audio parity tool (no game files)."""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

MOD_PATH = Path(__file__).resolve().parents[1] / "tools" / "check_rayman2_cd_audio_parity.py"
SPEC = importlib.util.spec_from_file_location("audio_cd_parity", MOD_PATH)
MOD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


def fake_pe(text_offset=0x200):
    # Tiny PE32 with equal virtual addresses, different physical offsets.
    rdata_offset = text_offset + 0x100
    data = bytearray(rdata_offset + 0x100)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", data, 0x84, 0x14C, 2, 0)
    struct.pack_into("<H", data, 0x94, 0xE0)
    struct.pack_into("<H", data, 0x98, 0x10B)
    struct.pack_into("<I", data, 0x98 + 0x1C, 0x400000)
    data[0x178:0x180] = b".text\0\0\0"
    struct.pack_into("<IIII", data, 0x180, 0x100, 0x1000, 0x100, text_offset)
    data[0x1A0:0x1A8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, 0x1A8, 0x100, 0x2000, 0x100, rdata_offset)
    data[text_offset:text_offset+0x100] = bytes(range(256))
    data[rdata_offset:rdata_offset+0x100] = b"A" * 0x100
    return data


class ParityTests(unittest.TestCase):
    def test_independent_pe_raw_offsets(self):
        a, b = MOD.PE32(fake_pe(0x200)), MOD.PE32(fake_pe(0x400))
        self.assertEqual(a.at(0x401020, 16), b.at(0x401020, 16))
        windows = MOD.compare_regions(a, b, (("test", 0x401020, 0x401040),))
        self.assertEqual([x["identical"] for x in windows], [True])

    def test_modified_byte_is_detected_without_printing_payload(self):
        a, b = fake_pe(0x200), fake_pe(0x400)
        b[0x400 + 0x28] ^= 0x01
        result = MOD.compare_regions(MOD.PE32(a), MOD.PE32(b),
                                     (("test", 0x401020, 0x401040),))
        self.assertFalse(result[0]["identical"])
        self.assertNotIn("data", result[0])

    def test_only_exact_pinned_pair_can_be_called_verified_cd(self):
        a, b = fake_pe(0x200), fake_pe(0x400)
        with self.assertRaisesRegex(ValueError, "fingerprints"):
            MOD.audit(a, b)
        result = MOD.audit(a, b, require_pins=False, check_anchors=False,
                           regions=(("test", 0x401020, 0x401040),))
        self.assertFalse(result["exact_pinned_pair"])
        self.assertFalse(result["original_cd_audio_path_confirmed"])
        self.assertTrue(result["selected_audio_code_equal"])

    def test_call_target_is_real_x86_relative_call(self):
        f = fake_pe()
        va = 0x401020
        f[0x220] = 0xE8
        struct.pack_into("<i", f, 0x221, 0x401050 - (va + 5))
        self.assertEqual(MOD.PE32(f).call_target(va), 0x401050)
        self.assertIsNone(MOD.PE32(f).call_target(va + 1))

    def test_unbacked_or_cross_section_reads_fail(self):
        a = MOD.PE32(fake_pe())
        with self.assertRaises(ValueError):
            a.at(0x4010FA, 12)
        with self.assertRaises(ValueError):
            a.at(0x4020F0, 32)
        with self.assertRaises(ValueError):
            MOD.compare_regions(a, a, (("bad", 0x401020, 0x401020),))

    def test_absolute_dword_xrefs_are_section_scoped_and_va_based(self):
        a = fake_pe(0x400)
        operand = struct.pack("<I", 0x4B9618)
        a[0x410:0x414] = operand
        a[0x490:0x494] = operand
        # Ignore the same literal in a different PE section.
        a[0x500:0x504] = operand
        pe = MOD.PE32(a)
        self.assertEqual(MOD.absolute_dword_xrefs(pe, 0x4B9618),
                         [0x401010, 0x401090])
        with self.assertRaisesRegex(ValueError, "not a 32-bit"):
            MOD.absolute_dword_xrefs(pe, 0x100000000)
        with self.assertRaisesRegex(ValueError, "Section not present"):
            MOD.absolute_dword_xrefs(pe, 0x4B9618, ".missing")

    def test_transaction_flag_requires_exact_known_code_locations(self):
        # Code and operand checks are separate: loose DWORD scanning alone
        # cannot prove that a hit was an actual machine-code instruction.
        self.assertEqual(len(MOD.THEME_TRANSACTION_OPS), 6)
        self.assertEqual(len(MOD.THEME_FLAG_OPERAND_VAS), 6)
        self.assertEqual(
            [op[1] for op in MOD.THEME_TRANSACTION_OPS],
            [0x459694, 0x4596E0, 0x45999D, 0x459B2C, 0x459BE5, 0x459C23]
        )
        self.assertEqual(MOD.THEME_TRANSACTION_FLAG_VA, 0x4B9618)
        for _, _va, op in MOD.THEME_TRANSACTION_OPS:
            self.assertIn(struct.pack("<I", 0x4B9618), op)

    def test_corrupted_headers_rejected(self):
        a = fake_pe()
        a[0:2] = b"NO"
        with self.assertRaises(ValueError):
            MOD.PE32(a)
        b = fake_pe()
        b[0x84:0x86] = b"\x64\x86"
        with self.assertRaises(ValueError):
            MOD.PE32(b)
        c = fake_pe()
        c[0x1A8+12:0x1A8+16] = b"\xff\xff\xff\x7f"
        with self.assertRaises(ValueError):
            MOD.PE32(c)


if __name__ == "__main__":
    unittest.main()
