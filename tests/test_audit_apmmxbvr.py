"""Synthetic PE32 fixtures only: no game binary is committed or executed."""
from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit_apmmxbvr as audit


def synthetic_pe(*, wrong_rva: bool = False) -> bytes:
    data = bytearray(0x6000)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x14C, 2, 0x38076FB4,
                     0, 0, 0xE0, 0x210E)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x10B)
    struct.pack_into("<I", data, opt+28, 0x400000)
    struct.pack_into("<I", data, opt+56, 0x9000)
    struct.pack_into("<I", data, opt+60, 0x400)
    struct.pack_into("<II", data, opt+96, 0x7000, 0x400)
    sect = opt + 0xE0
    data[sect:sect+8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, sect+8, 0x5000, 0x1000, 0x5000, 0x400)
    sect += 40
    data[sect:sect+8] = b".edata\0\0"
    struct.pack_into("<IIII", data, sect+8, 0x500, 0x7000, 0x500, 0x5400)
    edata = 0x5400
    struct.pack_into("<IIHHIIIIIII", data, edata, 0, 0, 0, 0,
                     0x7070, 1, 3, 3, 0x7040, 0x7050, 0x7060)
    entries = list(audit.EXPECTED_EXPORTS.items())
    names_offset = 0x7090
    for index, (name, fn_rva) in enumerate(entries):
        if index == 1 and wrong_rva:
            fn_rva += 0x10
        struct.pack_into("<I", data, 0x5440+4*index, fn_rva)
        struct.pack_into("<I", data, 0x5450+4*index, names_offset)
        struct.pack_into("<H", data, 0x5460+2*index, index)
        encoded = name.encode("ascii") + b"\0"
        file_offset = 0x5400+(names_offset-0x7000)
        data[file_offset:file_offset+len(encoded)] = encoded
        names_offset += len(encoded)
        code_off = 0x400 + (fn_rva - 0x1000)
        data[code_off:code_off+24] = b"\x90"*23+b"\xC3"
    data[0x5470:0x5470+len(b"APMMXbvr.dll\0")] = b"APMMXbvr.dll\0"
    return bytes(data)


class APMMXReadOnlyAuditTests(unittest.TestCase):
    def test_export_rvas_reconcile_to_owner_runtime(self):
        result = audit.audit_bytes(synthetic_pe())
        self.assertTrue(result["matches_owner_runtime"])
        self.assertEqual(result["file_bytes"], 0x6000)
        self.assertEqual(result["pe_timestamp_hex"], "0x38076fb4")
        self.assertEqual(result["export_count"], 3)
        self.assertEqual(len(result["functions"]), 3)
        self.assertTrue(all(f["first_24_bytes"] for f in result["functions"]))
        self.assertEqual([f["observed_rva"] for f in result["functions"]],
                         [0x54c0, 0x54d0, 0x54e0])

    def test_unexpected_export_rva_does_not_validate_as_owner_edition(self):
        result = audit.audit_bytes(synthetic_pe(wrong_rva=True))
        self.assertFalse(result["matches_owner_runtime"])
        self.assertFalse(result["functions"][1]["matches_runtime"])

    def test_hash_mismatch_fails_closed(self):
        with self.assertRaisesRegex(audit.PEFormatError, "SHA-256 mismatch"):
            audit.audit_bytes(synthetic_pe(), expected_sha256="0"*64)

    def test_invalid_machine_and_malformed_image_fail_closed(self):
        bad = bytearray(synthetic_pe())
        struct.pack_into("<H", bad, 0x84, 0x8664)
        with self.assertRaisesRegex(audit.PEFormatError, "machine"):
            audit.PE32(bytes(bad))
        bad = bytearray(synthetic_pe())
        struct.pack_into("<I", bad, 0x98+96+8, 0xFFFFFFFF)  # import dir only
        self.assertTrue(audit.audit_bytes(bytes(bad))["matches_owner_runtime"])
        bad = bytearray(synthetic_pe())
        struct.pack_into("<I", bad, 0x5400+20, 0xFFFFFFFF)  # functions count
        with self.assertRaisesRegex(audit.PEFormatError, "export counts"):
            audit.audit_bytes(bytes(bad))

    def test_truncated_export_sections_are_rejected(self):
        with self.assertRaises(audit.PEFormatError):
            audit.audit_bytes(synthetic_pe()[:0x5600])
        bad = bytearray(synthetic_pe())
        struct.pack_into("<I", bad, 0x178+20, 0xFFFFFF00)
        with self.assertRaises(audit.PEFormatError):
            audit.PE32(bytes(bad))

    def test_cli_has_stable_json_report_and_nonzero_exit_on_rva_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "APMMXbvr.dll"
            path.write_bytes(synthetic_pe())
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(audit.main([str(path), "--json"]), 0)
            self.assertTrue(json.loads(out.getvalue())["matches_owner_runtime"])
            path.write_bytes(synthetic_pe(wrong_rva=True))
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(audit.main([str(path)]), 1)

    def test_unknown_export_set_does_not_match_expected_owner_addresses(self):
        # No game binaries are embedded in this synthetic negative case.
        r = audit.audit_bytes(synthetic_pe(wrong_rva=True))
        self.assertFalse(r["matches_owner_runtime"])


if __name__ == "__main__":
    unittest.main()
