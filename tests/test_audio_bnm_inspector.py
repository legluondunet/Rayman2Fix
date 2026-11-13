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

def fake_bnm(event_count=2, resource_count=3, *, gap=0, reverse=False):
    # Independent offsets are encoded in header +4 and +12; the
    # game's 0x453BB8/0x453BC3 relocate both from bank header base.
    if reverse:
        resource_offset=44
        event_offset=resource_offset+resource_count*92+gap
    else:
        event_offset=44
        resource_offset=event_offset+event_count*32+gap
    end=max(44,event_offset+event_count*32,
            resource_offset+resource_count*92)
    raw=bytearray(end+40)
    struct.pack_into("<11I",raw,0,
                     1,event_offset,event_count,resource_offset,resource_count,
                     end,0,0,0,0,len(raw))
    for i in range(resource_count):
        p=resource_offset+i*92
        struct.pack_into("<III",raw,p,(23<<16)|i,1,0)
    if resource_count>=2:
        p=resource_offset+92
        struct.pack_into("<I",raw,p+4,11)
        struct.pack_into("<II",raw,p+12,10,60)
        raw[p+20]=127
        struct.pack_into("<Ii",raw,p+36,3,-1)
    for i in range(event_count):
        p=event_offset+i*32
        # A Play event may refer to a Theme resource by packed Id.
        struct.pack_into("<III",raw,p,(23<<16)|(100+i),1,
                         (23<<16)|(1 if i==0 else 900))
    return bytes(raw)

class BnmMetadataTests(unittest.TestCase):
    def test_theme_record_and_bank_id_not_filename_suffix(self):
        x = M.inspect_bytes(fake_bnm())
        self.assertEqual((x["events_declared"], x["resources_declared"]), (2,3))
        self.assertEqual(x["resource_type_counts"], {"1":2,"11":1})
        self.assertTrue(x["resource_table_indices_match_packed_ids"])
        self.assertEqual(x["candidate_bank_id_when_consistent"],23)
        self.assertEqual(x["candidate_reference_bnm_filename"],"bnk_23.BNM")
        self.assertTrue(x["filename_mapping_not_verified_on_original_cd"])
        self.assertEqual(x["table_ranges_file_relative"],
                         {"events":[44,108],"resources":[108,384]})
        self.assertEqual(x["event_type_counts"],{"1":2})
        self.assertEqual(len(x["local_play_events_referring_to_themes_candidate"]),1)
        self.assertEqual(
            x["play_event_references_candidate"][0]["candidate_resolution"],
            "local-theme")
        self.assertEqual(
            x["play_event_references_candidate"][1]["candidate_resolution"],
            "not-in-this-bank")
        t=x["theme_resources_candidate"][0]
        self.assertEqual(t["packed_id_hex"], "0x00170001")
        self.assertEqual((t["bank_group_from_id"],t["record_index_from_id"]), (23,1))
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

    def test_noncontiguous_and_reordered_tables(self):
        for reverse in (False, True):
            with self.subTest(reverse=reverse):
                x=M.inspect_bytes(fake_bnm(gap=35,reverse=reverse))
                self.assertEqual(x["resource_type_counts"],{"1":2,"11":1})
                self.assertEqual(
                    len(x["local_play_events_referring_to_themes_candidate"]),1)

    def test_reject_wrong_table_offsets_and_overlap(self):
        for field, bad in ((4,0),(12,44),(12,2000),(4,2000)):
            raw=bytearray(fake_bnm())
            struct.pack_into("<I",raw,field,bad)
            with self.subTest(field=field,bad=bad):
                with self.assertRaises(M.BNMError):
                    M.inspect_bytes(bytes(raw))

    def test_play_event_null_or_wrong_native_index_fails_closed(self):
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,44+8,0xFFFFFFFF)
        x=M.inspect_bytes(bytes(raw))
        self.assertEqual(
            x["play_event_references_candidate"][0]["candidate_resolution"],
            "null-resource-reference")
        # Event references index 1; modifying record 1's packed Id
        # must fail closed, NOT search another record by matching Id.
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,108+92,(23<<16)|5)
        x=M.inspect_bytes(bytes(raw))
        self.assertEqual(
            x["play_event_references_candidate"][0]["candidate_resolution"],
            "bank-id-or-record-index-mismatch")
        self.assertEqual(x["local_play_events_referring_to_themes_candidate"],[])
        self.assertFalse(x["resource_table_indices_match_packed_ids"])
        self.assertIsNone(x["candidate_reference_bnm_filename"])
        # Event references index 2: this points to a non-Theme record,
        # regardless of any other record's Id field.
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,44+8,(23<<16)|2)
        x=M.inspect_bytes(bytes(raw))
        self.assertEqual(
            x["play_event_references_candidate"][0]["candidate_resolution"],
            "local-non-theme")

    def test_declared_sizes_are_untrusted(self):
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,24,len(raw)+400)
        struct.pack_into("<I",raw,40,len(raw)+900)
        x=M.inspect_bytes(bytes(raw))
        self.assertTrue(x["warnings"])
        self.assertNotIn("file_integrity_ok",x)

if __name__=="__main__":
    unittest.main()
