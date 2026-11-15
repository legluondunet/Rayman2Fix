"""Synthetic-only tests for the original APMMX primary/auxiliary distinction."""
from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit_adpcm_primary_epoch as A


class SyntheticOwnerPE:
    machine = 0x14C
    timestamp = 0x38076F98
    image_base = 0x10000000
    image_size = 0xEE000

    def __init__(self, raw: bytes):
        self.sites = {rva: bytes.fromhex(opcodes)
                      for rva, opcodes in A.SITES.values()}

    def read_rva(self, rva: int, size: int) -> bytes:
        # The full byte window is checked at each original source site.
        for base, data in self.sites.items():
            if base <= rva and rva + size <= base + len(data):
                return data[rva-base:rva-base+size]
        raise A.PEFormatError("synthetic test requested unrecognized RVA")


class OriginalPrimaryEpochStaticTests(unittest.TestCase):
    def test_verifies_all_primary_and_auxiliary_sites_and_six_edges(self):
        fixture = b"synthetic PE without proprietary code"
        with (patch.object(A, "OWNER_ORIGINAL_SHA256", hashlib.sha256(fixture).hexdigest()),
              patch.object(A, "PE32", SyntheticOwnerPE)):
            report = A.static_audit(fixture)
        self.assertEqual(report["verified_original_opcode_anchors"], 24)
        self.assertEqual(report["verified_original_control_flow_edges"], 6)
        self.assertEqual(report["successful_handle_output_rva"], "0x1599")
        self.assertTrue(report["one_primary_allocation_can_invoke_initializer_more_than_once"])
        self.assertTrue(report["initializer_call_is_not_an_epoch"])
        self.assertFalse(report["permission_to_modify_original_audio"])

    def test_unknown_original_is_rejected_before_parsing(self):
        with patch.object(A, "PE32") as parser:
            with self.assertRaisesRegex(A.PEFormatError, "SHA-256 mismatch"):
                A.static_audit(b"not the user's original DLL")
            parser.assert_not_called()

    def test_mutated_allocator_opcode_fails_closed(self):
        fixture = b"synthetic opcode mismatch"
        class MutatedPE(SyntheticOwnerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.sites[0x158A] = b"\x90" * 7
        with (patch.object(A, "OWNER_ORIGINAL_SHA256", hashlib.sha256(fixture).hexdigest()),
              patch.object(A, "PE32", MutatedPE)):
            with self.assertRaisesRegex(A.PEFormatError, "failure_clears_handle"):
                A.static_audit(fixture)

    def test_wrong_pe_metadata_fails_closed(self):
        fixture = b"synthetic version mismatch"
        class WrongPE(SyntheticOwnerPE):
            timestamp = 123
        with (patch.object(A, "OWNER_ORIGINAL_SHA256", hashlib.sha256(fixture).hexdigest()),
              patch.object(A, "PE32", WrongPE)):
            with self.assertRaisesRegex(A.PEFormatError, "metadata mismatch"):
                A.static_audit(fixture)

    def test_branch_targets_cover_loop_and_failure(self):
        self.assertEqual(A.RELATIVE_EDGES["loop_second_initializer"][0], 0x14CF)
        self.assertEqual(A.RELATIVE_EDGES["initializer_failure_branch"][0], 0x158A)
        self.assertEqual(A.RELATIVE_EDGES["auxiliary_one_initializer"][0], 0x1FC0)
        self.assertEqual(A.RELATIVE_EDGES["auxiliary_two_initializer"][0], 0x1FC0)
        self.assertIn("success_writes_output_handle", A.SITES)


class SyntheticEpochModelTests(unittest.TestCase):
    def test_auxiliary_initialization_is_not_a_new_primary_epoch(self):
        model = A.SyntheticEpochModel()
        old = model.primary_commit(3)
        model.auxiliary_init(3)
        model.auxiliary_init(3)
        self.assertTrue(model.matches_conceptual_epoch(3, old))
        self.assertEqual(model.serial, old)

    def test_same_handle_reuse_changes_epoch_even_without_stop_or_count_change(self):
        model = A.SyntheticEpochModel()
        before = model.primary_commit(0)
        after = model.primary_commit(0)
        self.assertNotEqual(before, after)
        self.assertFalse(model.matches_conceptual_epoch(0, before))
        self.assertTrue(model.matches_conceptual_epoch(0, after))
        self.assertEqual(len(model.handles), 1)

    def test_failed_destructive_reinitialization_invalidates_old_epoch(self):
        model = A.SyntheticEpochModel()
        old = model.primary_commit(7)
        model.failed_destructive_reinit(7)
        self.assertFalse(model.matches_conceptual_epoch(7, old))
        with self.assertRaisesRegex(ValueError, "unknown primary"):
            model.auxiliary_init(7)

    def test_retirement_and_unknown_external_change_are_fail_closed(self):
        model = A.SyntheticEpochModel()
        first = model.primary_commit(1)
        second = model.primary_commit(2)
        model.retire(1)
        self.assertFalse(model.matches_conceptual_epoch(1, first))
        self.assertTrue(model.matches_conceptual_epoch(2, second))
        model.unknown_lifecycle_event()
        self.assertFalse(model.matches_conceptual_epoch(2, second))


if __name__ == "__main__":
    unittest.main()
