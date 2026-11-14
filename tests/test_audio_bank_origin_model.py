"""Synthetic-only behavioral tests of native loaded-bank inversion.

The 1999 PC executable has a 72-byte bank descriptor list and a 92-byte
resource table stride. These fixtures test safe reverse resolution only;
no proprietary CD data and no actual Wine execution.
"""
import importlib.util
from pathlib import Path
import sys
import unittest

FILE = Path(__file__).resolve().parents[1] / "tools" / "model_native_audio_bank_origin.py"
SPEC = importlib.util.spec_from_file_location("native_bank_origin_model", FILE)
M = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = M
SPEC.loader.exec_module(M)


class NativeBankOriginTests(unittest.TestCase):
    def fixture(self):
        descriptors = {
            0x100100: M.BankDescriptor(3, 1, 0x110100, 0x100200),
            0x100200: M.BankDescriptor(19, 1, 0x110200, 0x100300),
            0x100300: M.BankDescriptor(71, 1, 0x110300, 0),
        }
        heads = {
            0x110100: M.BankHead(0x200000, 2),
            0x110200: M.BankHead(0x300000, 16),
            0x110300: M.BankHead(0x400000, 2),
        }
        return descriptors, heads

    def test_exact_native_address_recovers_bank_and_table_index(self):
        desc, heads = self.fixture()
        result = M.invert_native_loaded_bank_registry(
            0x100100, 0x300000 + 4*92, desc, heads)
        self.assertEqual(result, M.BankOrigin(19, 4, 1, True, 3))
        # Native record's Id can be ANYTHING: algorithm doesn't read it.

    def test_rejects_unaligned_and_out_of_table_address(self):
        desc, heads = self.fixture()
        for pointer in (0x300001, 0x300000 + 16*92, 0x2FFFFF,
                        0x0, -1, 0x1_0000_0000):
            with self.subTest(pointer=pointer):
                result=M.invert_native_loaded_bank_registry(
                    0x100100,pointer,desc,heads)
                self.assertEqual(result.bank_id,-1)
                self.assertEqual(result.resource_index,-1)

    def test_inactive_bank_cannot_claim_provenance(self):
        desc, heads = self.fixture()
        desc[0x100200] = M.BankDescriptor(19, 2, 0x110200, 0x100300)
        result=M.invert_native_loaded_bank_registry(
            0x100100,0x300000+92*4,desc,heads)
        self.assertEqual(result.bank_id,-1)
        self.assertTrue(result.list_complete)

    def test_overlapping_loaded_tables_make_origin_unknown(self):
        desc,heads=self.fixture()
        desc[0x100300] = M.BankDescriptor(71,1,0x110300,0)
        heads[0x110300] = M.BankHead(0x300000,16)
        result=M.invert_native_loaded_bank_registry(
            0x100100,0x300000+92*4,desc,heads)
        self.assertEqual(result.candidates,2)
        self.assertTrue(result.list_complete)
        self.assertEqual(result.bank_id,-1)

    def test_cyclic_linked_list_fails_closed(self):
        desc,heads=self.fixture()
        desc[0x100300] = M.BankDescriptor(71,1,0x110300,0x100200)
        result=M.invert_native_loaded_bank_registry(
            0x100100,0x300000+92*4,desc,heads)
        self.assertEqual(result.bank_id,-1)
        self.assertFalse(result.list_complete)
        self.assertEqual(result.nodes_scanned,3)

    def test_exceeded_scan_cap_fails_closed(self):
        desc,heads=self.fixture()
        for cap in (1,2,3):
            with self.subTest(cap=cap):
                result=M.invert_native_loaded_bank_registry(
                    0x100100,0x300000+92*4,desc,heads,max_nodes=cap)
                self.assertFalse(result.list_complete)
                self.assertEqual(result.bank_id,-1)

    def test_broken_loaded_head_fails_closed_even_if_other_bank_matches(self):
        desc,heads=self.fixture()
        del heads[0x110300]
        result=M.invert_native_loaded_bank_registry(
            0x100100,0x300000+92*4,desc,heads)
        self.assertEqual(result.candidates,1)
        self.assertFalse(result.list_complete)
        self.assertEqual(result.bank_id,-1)

    def test_bad_bank_id_fails_closed(self):
        desc,heads=self.fixture()
        desc[0x100200]=M.BankDescriptor(0x10000,1,0x110200,0x100300)
        result=M.invert_native_loaded_bank_registry(
            0x100100,0x300000+92*4,desc,heads)
        self.assertEqual(result.candidates,1)
        self.assertTrue(result.list_complete)
        self.assertEqual(result.bank_id,-1)

    def test_terminated_empty_list_does_not_claim_a_bank(self):
        result=M.invert_native_loaded_bank_registry(
            0,0x300000,{}, {})
        self.assertEqual(result,M.BankOrigin(list_complete=True))

    def test_bad_loaded_bank_resource_metadata_fails_closed(self):
        for first,count in ((0,1),(0x300000,65537)):
            with self.subTest(first=first,count=count):
                desc,heads=self.fixture()
                heads[0x110300] = M.BankHead(first,count)
                result=M.invert_native_loaded_bank_registry(
                    0x100100,0x300000+92*4,desc,heads)
                self.assertFalse(result.list_complete)
                self.assertEqual(result.bank_id,-1)


if __name__ == "__main__":
    unittest.main()
