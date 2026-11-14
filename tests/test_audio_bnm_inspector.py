"""Synthetic-only tests for the read-only CPA BNM metadata inspector.

The record's on-disk +0 Id is NOT the table lookup key: original-CD native
0x453F90 uses (resource_ref >> 16) to choose a bank, then resource_ref & 0xFFFF
to address an array record. Test intentionally unrelated on-disk Ids.
"""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools" / "inspect_rayman2_bnm.py"
SPEC = importlib.util.spec_from_file_location("rayman2_bnm", PATH)
assert SPEC and SPEC.loader
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def fake_bnm(event_count=2, resource_count=3, *, gap=0, reverse=False):
    # Independent original-CD offsets are at header +4 and +12.
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
        # The actual +0 record Id field need not equal (bank << 16) | index.
        struct.pack_into("<III",raw,p,(88<<16)|(300+i),1,0)
    if resource_count>=2:
        p=resource_offset+92
        struct.pack_into("<I",raw,p+4,11)
        struct.pack_into("<II",raw,p+12,10,60)
        raw[p+20]=127
        struct.pack_into("<Ii",raw,p+36,3,-1)
    for i in range(event_count):
        p=event_offset+i*32
        # Event id field is unrelated; its reference at +8 uses a
        # bank/index pair addressed by the original x86 lookup.
        struct.pack_into("<III",raw,p,(99<<16)|(400+i),1,
                         (23<<16)|(1 if i==0 else 900))
    return bytes(raw)


class BnmMetadataTests(unittest.TestCase):
    def test_theme_record_uses_provided_bank_hint_and_native_index(self):
        x=M.inspect_bytes(fake_bnm(), bank_id_hint=23)
        self.assertEqual((x["events_declared"], x["resources_declared"]), (2,3))
        self.assertEqual(x["resource_type_counts"], {"1":2,"11":1})
        self.assertTrue(x["resource_id_field_not_required_to_match_native_table_index"])
        self.assertEqual(x["candidate_bank_id_from_filename_hint"],23)
        self.assertEqual(x["candidate_reference_bnm_filename"],"bnk_23.BNM")
        self.assertTrue(x["filename_is_not_bank_authenticity_proof"])
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
        self.assertEqual(t["table_index"],1)
        self.assertEqual(t["record_id_field_hex"], "0x0058012D")
        self.assertTrue(t["record_id_field_is_not_a_native_lookup_key"])
        self.assertEqual((t["number_of_parts_candidate"],t["outro_offset_candidate"]),
                         (3,-1))
        self.assertEqual(t["volume_byte_candidate"],127)
        self.assertFalse(x["audio_extracted"])
        self.assertFalse(x["verified_against_actual_retail_cd_bnm"])

    def test_no_bank_provenance_must_not_claim_local_theme(self):
        x=M.inspect_bytes(fake_bnm())  # no file name; no trusted bank identity
        self.assertIsNone(x["candidate_bank_id_from_filename_hint"])
        self.assertIsNone(x["candidate_reference_bnm_filename"])
        self.assertEqual(x["local_play_events_referring_to_themes_candidate"],[])
        self.assertEqual(x["play_event_references_candidate"][0][
            "candidate_resolution"],"unresolved-bank-provenance")
        self.assertEqual(x["play_event_references_candidate"][1][
            "candidate_resolution"],"unresolved-bank-provenance")

    def test_empty_bank_and_overrun(self):
        self.assertEqual(M.inspect_bytes(fake_bnm(1,0))["candidate_metadata_end"],76)
        self.assertEqual(M.inspect_bytes(fake_bnm(1,0))["theme_resources_candidate"],[])
        for raw in (b"BNM", fake_bnm()[:100]):
            with self.assertRaises(M.BNMError):
                M.inspect_bytes(raw)
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,8,0xFFFFFFFF)
        with self.assertRaises(M.BNMError):
            M.inspect_bytes(bytes(raw))

    def test_noncontiguous_and_reordered_tables(self):
        for reverse in (False, True):
            with self.subTest(reverse=reverse):
                x=M.inspect_bytes(fake_bnm(gap=35,reverse=reverse),
                                  bank_id_hint=23)
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

    def test_native_reference_resolution_ignores_disk_id(self):
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,44+8,0xFFFFFFFF)
        x=M.inspect_bytes(bytes(raw),bank_id_hint=23)
        self.assertEqual(x["play_event_references_candidate"][0][
            "candidate_resolution"],"null-resource-reference")

        # A wrong bank cannot be resolved locally even if its low 16 bits
        # select a valid record in this file.
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,44+8,(24<<16)|1)
        x=M.inspect_bytes(bytes(raw),bank_id_hint=23)
        self.assertEqual(x["play_event_references_candidate"][0][
            "candidate_resolution"],"different-bank")

        # Native lookup ignores resource record.Id, which is editor data.
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,108+92,0xDEADBEEF)
        x=M.inspect_bytes(bytes(raw),bank_id_hint=23)
        self.assertEqual(x["play_event_references_candidate"][0][
            "candidate_resolution"],"local-theme")
        self.assertEqual(x["theme_resources_candidate"][0][
            "record_id_field_hex"],"0xDEADBEEF")

        # Lookup index 2 is a sample, despite unrelated record editor IDs.
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,44+8,(23<<16)|2)
        x=M.inspect_bytes(bytes(raw),bank_id_hint=23)
        self.assertEqual(x["play_event_references_candidate"][0][
            "candidate_resolution"],"local-non-theme")

    def test_bank_name_is_untrusted_hint_not_internal_id(self):
        with tempfile.TemporaryDirectory() as d:
            named=Path(d)/"BNK_23.bnm"
            renamed=Path(d)/"unknown.bnm"
            named.write_bytes(fake_bnm())
            renamed.write_bytes(fake_bnm())
            x=M.inspect_file(named)
            self.assertEqual(x["candidate_bank_id_from_filename_hint"],23)
            self.assertTrue(x["filename_is_not_bank_authenticity_proof"])
            self.assertEqual(len(x["local_play_events_referring_to_themes_candidate"]),1)
            z=M.inspect_file(renamed)
            self.assertIsNone(z["candidate_bank_id_from_filename_hint"])
            self.assertEqual(z["local_play_events_referring_to_themes_candidate"],[])
            invalid=Path(d)/"BNK_65536.bnm"
            invalid.write_bytes(fake_bnm())
            with self.assertRaises(M.BNMError):
                M.inspect_file(invalid)

    def test_declared_sizes_are_untrusted(self):
        raw=bytearray(fake_bnm())
        struct.pack_into("<I",raw,24,len(raw)+400)
        struct.pack_into("<I",raw,40,len(raw)+900)
        x=M.inspect_bytes(bytes(raw))
        self.assertTrue(x["warnings"])
        self.assertNotIn("file_integrity_ok",x)


if __name__=="__main__":
    unittest.main()
