"""Synthetic-only tests for the experimental BNM metadata reader."""
import importlib.util
from pathlib import Path
import struct
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools" / "inspect_rayman2_bnm.py"
SPEC = importlib.util.spec_from_file_location("rayman2_bnm", PATH)
assert SPEC and SPEC.loader
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)

def fake_bnm(event_count=2, resource_count=3):
    end = 44 + event_count * 32 + resource_count * 92
    raw = bytearray(end + 40)
    struct.pack_into("<11I", raw, 0,
                     1, 0, event_count, 0, resource_count,
                     end, 0, 0, 0, 0, len(raw))
    start = 44 + event_count * 32
    for i in range(resource_count):
        struct.pack_into("<III", raw, start + 92*i,
                         (23 << 16) | (i + 2), 1, 0)
    if resource_count >= 2:
        p = start + 92
        struct.pack_into("<I", raw, p+4, 11)
        raw[p+20] = 127
        struct.pack_into("<Ii", raw, p+36, 3, -1)
    return bytes(raw)

class BnmMetadataTests(unittest.TestCase):
    def test_theme_record_and_bank_id_not_filename_suffix(self):
        x = M.inspect_bytes(fake_bnm())
        self.assertEqual((x["events_declared"], x["resources_declared"]), (2,3))
        self.assertEqual(x["resource_type_counts"], {"1":2,"11":1})
        t=x["theme_resources_candidate"][0]
        self.assertEqual(t["packed_id_hex"], "0x00170003")
        self.assertEqual((t["bank_group_from_id"],t["record_index_from_id"]), (23,3))
        self.assertEqual((t["number_of_parts_candidate"],t["outro_offset_candidate"]), (3,-1))
        self.assertEqual(t["volume_byte_candidate"], 127)
        self.assertFalse(x["audio_extracted"])
        self.assertFalse(x["verified_against_actual_retail_cd_bnm"])

    def test_empty_bank_and_overrun(self):
        self.assertEqual(M.inspect_bytes(fake_bnm(1,0))["candidate_metadata_end"],76)
        self.assertEqual(M.inspect_bytes(fake_bnm(1,0))["theme_resources_candidate"],[])
        for raw in (b"BNM", fake_bnm()[:100]):
            with self.assertRaises(M.BNMError): M.inspect_bytes(raw)
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,8,0xFFFFFFFF)
        with self.assertRaises(M.BNMError): M.inspect_bytes(bytes(raw))

    def test_declared_sizes_are_untrusted(self):
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,24,len(raw)+400)
        struct.pack_into("<I",raw,40,len(raw)+900)
        x=M.inspect_bytes(bytes(raw))
        self.assertTrue(x["warnings"])
        self.assertNotIn("file_integrity_ok",x)

if __name__=="__main__":
    unittest.main()
