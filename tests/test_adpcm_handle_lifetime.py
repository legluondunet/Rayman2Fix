"""Synthetic-only regression tests for original APMMX handle lifetime evidence.

No copyrighted Rayman 2 DLL is embedded, loaded, invoked or committed.
The conceptual token is deliberately NOT read from a native ADPCM record.
"""
from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import audit_adpcm_handle_lifetime as A


class SyntheticVerifiedPE:
    machine=0x14C
    timestamp=0x38076F98
    image_base=0x10000000
    image_size=0xEE000

    def __init__(self,raw: bytes):
        self.sites={rva:bytes.fromhex(opcode)
                    for _,rva,opcode in A.SITES}

    def read_rva(self,rva: int,size: int) -> bytes:
        return self.sites[rva][:size]


class AdpcmHandleLifetimeTests(unittest.TestCase):
    def test_exact_original_static_sites_cover_all_three_alias_paths(self):
        self.assertEqual(len(A.SITES),16)
        self.assertEqual(len({name for name,_,_ in A.SITES}),16)
        self.assertEqual(len({rva for _,rva,_ in A.SITES}),16)
        names={name for name,_,_ in A.SITES}
        for required in ("recycle_existing_record_handle",
                         "first_free_handle_table",
                         "invalidate_stopped_handle",
                         "rebase_moved_record_pointer_by_0xe4",
                         "record_becomes_inactive",
                         "record_backref_to_handle",
                         "compact_record_array"):
            self.assertIn(required,names)

    def test_wrong_hash_prevents_any_version_dependent_code_read(self):
        with patch.object(A,"PE32") as reader:
            with self.assertRaisesRegex(A.PEFormatError,"SHA-256 mismatch"):
                A.static_audit(b"synthetic only")
            reader.assert_not_called()

    def test_independent_synthetic_opcode_probe_proves_parser_fail_closed(self):
        data=b"fake PE fixture, never original game bytes"
        checksum=hashlib.sha256(data).hexdigest()
        with patch.object(A,"PE32",SyntheticVerifiedPE):
            report=A.static_audit(data,expected_sha256=checksum)
        self.assertEqual(report["original_sites_verified"],16)
        self.assertEqual(report["max_handle_slots"],30)
        self.assertEqual(report["record_stride_bytes"],0xE4)
        self.assertFalse(report["permission_to_change_original_audio"])

    def test_mismatched_original_opcode_refuses_semantic_claim(self):
        data=b"fake PE opcodes"
        checksum=hashlib.sha256(data).hexdigest()

        class WrongOpcode(SyntheticVerifiedPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.sites[0x14BE]=b"\x90"*6

        with patch.object(A,"PE32",WrongOpcode):
            with self.assertRaisesRegex(A.PEFormatError,"recycle_existing_record_handle"):
                A.static_audit(data,expected_sha256=checksum)

    def test_wrong_original_pe_metadata_rejected(self):
        data=b"fake PE header"
        checksum=hashlib.sha256(data).hexdigest()

        class WrongPE(SyntheticVerifiedPE):
            timestamp=0x12345678

        with patch.object(A,"PE32",WrongPE):
            with self.assertRaisesRegex(A.PEFormatError,"PE version mismatch"):
                A.static_audit(data,expected_sha256=checksum)

    def test_stop_then_new_audio_can_alias_all_observed_fields(self):
        m=A.HandleTableModel()
        previous=m.play_new("old music")
        observed=m.observable(previous)
        m.stop(previous)
        self.assertEqual(m.observable(previous)[1],-1)
        current=m.play_new("different new music")
        self.assertEqual(previous,current)
        self.assertEqual(observed,m.observable(current))
        self.assertEqual(m.records[0].token,"different new music")

    def test_finished_record_reuse_without_stop_is_a_second_aba(self):
        m=A.HandleTableModel()
        old=m.play_new("old music")
        prior=m.observable(old)
        m.mark_reusable(old)
        self.assertEqual(m.play_in_place("new track",old),old)
        self.assertEqual(prior,m.observable(old))
        self.assertEqual(m.records[0].token,"new track")
        self.assertTrue(m.records[0].active)

    def test_in_place_reuse_requires_inactive_record(self):
        m=A.HandleTableModel()
        h=m.play_new("active song")
        with self.assertRaisesRegex(ValueError,"no eligible"):
            m.play_in_place("other",h)
        m.mark_reusable(h)
        self.assertFalse(m.records[0].active)
        m.play_in_place("other",h)
        with self.assertRaisesRegex(ValueError,"no eligible"):
            m.play_in_place("third",h)

    def test_stop_rebases_surviving_handles_and_pool_indices(self):
        m=A.HandleTableModel()
        first=m.play_new("A")
        second=m.play_new("B")
        third=m.play_new("C")
        self.assertEqual((m.slots[first],m.slots[second],m.slots[third]),
                         (0,1,2))
        m.stop(first)
        self.assertEqual((m.slots[first],m.slots[second],m.slots[third]),
                         (None,0,1))
        self.assertEqual(m.observable(second),(second,0,2,True))
        self.assertEqual(m.observable(third),(third,1,2,True))
        m.validate()

    def test_old_ordinal_now_identifies_unrelated_survivor(self):
        m=A.HandleTableModel()
        old=m.play_new("old voice index 0")
        other=m.play_new("surviving different voice")
        m.stop(old)
        self.assertEqual(m.observable(other)[1],0)
        self.assertNotEqual(m.records[0].token,"old voice index 0")

    def test_first_free_registry_slot_is_reused_even_with_live_survivor(self):
        m=A.HandleTableModel()
        freed=m.play_new("first")
        existing=m.play_new("second")
        m.stop(freed)
        new=m.play_new("third")
        self.assertEqual(new,freed)
        self.assertEqual(m.observable(existing)[1],0)
        self.assertEqual(m.observable(new)[1],1)
        self.assertTrue(m.observable(new)[3])

    def test_full_thirty_slot_table_and_reuse_stays_consistent(self):
        m=A.HandleTableModel()
        handles=[m.play_new(f"track-{i}") for i in range(30)]
        self.assertEqual(len(set(handles)),30)
        self.assertEqual(len(m.records),30)
        with self.assertRaisesRegex(ValueError,"at most 30"):
            m.play_new("too many")
        for i in reversed(range(0,30,2)):
            m.stop(handles[i])
        m.validate()
        self.assertEqual(len(m.records),15)
        for i in range(15):
            m.play_new(f"replacement-{i}")
        m.validate()
        self.assertEqual(len(m.records),30)

    def test_illustrative_not_original_generation_counter(self):
        result=A.demonstrate_aliases()
        self.assertTrue(result["illustrative_model_only"])
        self.assertTrue(result["stop_then_new_voice_can_reuse_same_handle_and_ordinal"]["different_logical_tracks"])
        self.assertFalse(result["inactive_record_can_be_reinitialized_without_stop"]["native_stop_required"])
        self.assertIn("NEVER authorize",result["rule"])

    def test_tool_never_invokes_original_audio_or_writes_binary(self):
        code=(Path(__file__).resolve().parents[1]/
              "tools/audit_adpcm_handle_lifetime.py").read_text(encoding="utf-8")
        for bad in ("ctypes","subprocess","os.system","LoadLibrary",
                    "WriteProcessMemory", "VirtualProtect", ".write_bytes("):
            self.assertNotIn(bad,code)


if __name__=="__main__":
    unittest.main()
