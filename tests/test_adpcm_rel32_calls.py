"""Synthetic regressions for read-only APMMX rel32 call-site candidates.

No proprietary game/DLL bytes in fixtures; the original DLL is never
loaded or executed.
"""
from __future__ import annotations

import hashlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit_adpcm_rel32_calls as A


class SyntheticPE:
    machine = 0x14C
    timestamp = 0x38076F98
    image_base = 0x10000000
    image_size = 0xEE000

    def __init__(self, data):
        self.sections = [(".text", 0x1000, 0xD800, 0, 0xD800)]
        self.text = bytearray(b"\x90" * 0xD800)
        for target, sites in A.EXPECTED.items():
            for site in sites:
                offset = site-0x1000
                self.text[offset] = 0xE8
                struct.pack_into("<i", self.text, offset+1, target-(site+5))

    def read_rva(self, rva, size):
        if rva != 0x1000 or size != 0xD800:
            raise A.PEFormatError("unexpected synthetic request")
        return bytes(self.text)


class Rel32CallSiteTests(unittest.TestCase):
    def test_exact_expected_candidates_on_synthetic_section(self):
        pe = SyntheticPE(b"fake")
        for target, expected_sites in A.EXPECTED.items():
            self.assertEqual(A.candidates(bytes(pe.text),0x1000,target),expected_sites)

    def test_signed_relative_displacement_and_section_boundaries(self):
        raw = bytearray(b"\x90"*20)
        raw[0] = 0xE8
        struct.pack_into("<i", raw, 1, 15)
        raw[10] = 0xE8
        struct.pack_into("<i", raw, 11, -15)
        self.assertEqual(A.candidates(raw,0x1000,0x1014),(0x1000,))
        self.assertEqual(A.candidates(raw,0x1000,0x1000),(0x100A,))
        self.assertEqual(A.candidates(raw[:4],0x1000,0x1014),())

    def test_original_hash_rejected_before_pe_parse(self):
        with patch.object(A, "PE32") as pe:
            with self.assertRaisesRegex(A.PEFormatError,"SHA-256 mismatch"):
                A.audit(b"synthetic only")
            pe.assert_not_called()

    def test_synthetic_whole_audit_and_no_permission_to_patch(self):
        fixture = b"not an original DLL"
        with (patch.object(A,"OWNER_ORIGINAL_SHA256",hashlib.sha256(fixture).hexdigest()),
              patch.object(A,"PE32",SyntheticPE)):
            report = A.audit(fixture)
        self.assertTrue(report["raw_direct_call_candidates_verified"])
        self.assertFalse(report["native_generation_proven"])
        self.assertFalse(report["permission_to_modify_audio"])
        self.assertEqual(len(report["direct_rel32_candidates"]["0x15D0"]),4)
        self.assertEqual(len(report["direct_rel32_candidates"]["0x1FC0"]),3)
        self.assertEqual(report["direct_target_count"],8)
        self.assertEqual(report["direct_callsite_count"],23)
        self.assertEqual(report["direct_rel32_candidates"]["0x1F50"],["0x290E"])
        self.assertEqual(report["direct_rel32_candidates"]["0x2320"],["0x2A20"])
        self.assertEqual(report["direct_rel32_candidates"]["0x13A0"],["0x27BE","0x3223"])
        self.assertTrue(report["query_internal_helpers_have_one_direct_candidate_each"])
        self.assertFalse(report["unique_original_query_runtime_invocation_proven"])
        self.assertFalse(report["indirect_or_dynamic_call_paths_exhausted"])

    def test_missing_or_extra_candidate_is_fail_closed(self):
        class MissingCandidate(SyntheticPE):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x172C-0x1000] = 0x90
        fixture=b"synthetic missing one"
        with (patch.object(A,"OWNER_ORIGINAL_SHA256",hashlib.sha256(fixture).hexdigest()),
              patch.object(A,"PE32",MissingCandidate)):
            with self.assertRaisesRegex(A.PEFormatError,"differ from"):
                A.audit(fixture)

    def test_primary_allocator_is_only_a_two_site_direct_call_target(self):
        self.assertEqual(A.EXPECTED[0x13A0], (0x27BE,0x3223))
        self.assertEqual(A.EXPECTED[0x1FC0], (0x14F9,0x21F3,0x22A3))
        # The shared initializer is not a top-level creation epoch.
        self.assertNotEqual(A.EXPECTED[0x13A0], A.EXPECTED[0x1FC0])

    def test_query_helpers_each_have_one_direct_original_candidate(self):
        self.assertEqual(A.EXPECTED[0x1F50], (0x290E,))
        self.assertEqual(A.EXPECTED[0x2320], (0x2A20,))
        self.assertEqual(A.EXPECTED[0x2900], (0x5155,))
        self.assertEqual(A.EXPECTED[0x2A00], (0x51A5,))
        self.assertEqual(len(A.EXPECTED[0x2480]),10)

    def test_extra_direct_getpos_candidate_rejected_even_with_original_sites(self):
        class ExtraGetPos(SyntheticPE):
            def __init__(self,data):
                super().__init__(data)
                pos=0x70F0-0x1000
                self.text[pos]=0xE8
                struct.pack_into("<i",self.text,pos+1,0x2320-(0x70F0+5))
        fixture=b"synthetic extra GetPos call"
        with (patch.object(A,"OWNER_ORIGINAL_SHA256",hashlib.sha256(fixture).hexdigest()),
              patch.object(A,"PE32",ExtraGetPos)):
            with self.assertRaisesRegex(A.PEFormatError,"differ from"):
                A.audit(fixture)

    def test_extra_direct_cleanup_candidate_rejected_even_with_original_sites(self):
        class ExtraCleanup(SyntheticPE):
            def __init__(self,data):
                super().__init__(data)
                pos=0x7A10-0x1000
                self.text[pos]=0xE8
                struct.pack_into("<i",self.text,pos+1,0x1F50-(0x7A10+5))
        fixture=b"synthetic extra TestIsPlaying cleanup"
        with (patch.object(A,"OWNER_ORIGINAL_SHA256",hashlib.sha256(fixture).hexdigest()),
              patch.object(A,"PE32",ExtraCleanup)):
            with self.assertRaisesRegex(A.PEFormatError,"differ from"):
                A.audit(fixture)

    def test_raw_e8_candidates_are_not_treated_as_decoded_instruction_proof(self):
        raw=bytearray(b"\x90"*12)
        raw[3]=0xE8
        struct.pack_into("<i",raw,4,0x2000-(0x1003+5))
        self.assertEqual(A.candidates(raw,0x1000,0x2000),(0x1003,))
        self.assertIn("NOT a", A.__doc__)


if __name__=="__main__":
    unittest.main()
