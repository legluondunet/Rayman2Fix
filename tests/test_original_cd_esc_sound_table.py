"""Original CD script ESC 64-slot pause preflight: synthetic-only guards."""
from __future__ import annotations
import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_original_cd_esc_sound_table as A

FAKE=b'Synthetic original CD source table only; no game file bytes'

class FakeCD:
    machine=0x14C
    timestamp=A.ORIGINAL_PE_TIMESTAMP
    image_base=A.ORIGINAL_IMAGE_BASE
    image_size=A.ORIGINAL_IMAGE_SIZE
    def __init__(self, data):
        self.code=bytearray(bytes([0x90])*0xA0000)
        for sites in (A.PAUSE,A.RESUME):
            for rva,hexcode in sites.values():
                raw=bytes.fromhex(hexcode)
                self.code[rva:rva+len(raw)]=raw
    def read_rva(self,rva,n):
        if rva<0 or n<0 or rva+n>len(self.code):
            raise A.PEFormatError('fake CD code bounds')
        return bytes(self.code[rva:rva+n])

def synthetic_audit(pe=FakeCD):
    with (patch.object(A,'ORIGINAL_SHA256',hashlib.sha256(FAKE).hexdigest()),
          patch.object(A,'PE32',pe)):
        return A.audit(FAKE)

def records():
    return [A.Entry(field14=0,field08=0,field1c=0,field10=0,
                    source_ref=0) for _ in range(A.SOURCE_ENTRY_COUNT)]

class SoundTableTests(unittest.TestCase):
    def test_original_sound_script_paths_verified_and_no_policy_authority(self):
        result=synthetic_audit()
        self.assertEqual(result['pause_opcode_windows_verified'],14)
        self.assertEqual(result['resume_opcode_windows_verified'],12)
        self.assertEqual(result['direct_call_edges_verified'],8)
        self.assertEqual(result['original_source_slots'],64)
        self.assertEqual(result['original_source_stride_bytes'],32)
        self.assertTrue(result['source_pause_eligibility_before_dispatch_readable'])
        self.assertFalse(result['all_entry_states_atomic_preflight_proven'])
        self.assertFalse(result['same_native_adpcm_generation_before_dispatch_proven'])
        self.assertFalse(result['safe_to_suppress_pending_theme_pause'])
        self.assertFalse(result['native_driver_calls_or_hooks_added'])

    def test_unknown_cd_binary_rejected_before_pe(self):
        with patch.object(A,'PE32') as pe:
            with self.assertRaisesRegex(A.PEFormatError,'SHA-256'):
                A.audit(b'wrong exe')
            pe.assert_not_called()

    def test_mismatched_audio_script_branch_refused(self):
        class Bad(FakeCD):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x580CA]=0x90
        with self.assertRaisesRegex(A.PEFormatError,'source_field1c_zero_required'):
            synthetic_audit(Bad)

    def test_transition_callback_call_target_mutation_refused(self):
        class Bad(FakeCD):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x580E2]=0x00
        with self.assertRaisesRegex(A.PEFormatError,'source_transition_pause'):
            synthetic_audit(Bad)

    def test_pause_and_resume_scans_share_same_base_count_and_stride(self):
        self.assertEqual(A.SOURCE_TABLE_VA,0x4B8C98)
        self.assertEqual(A.SOURCE_ENTRY_COUNT,64)
        self.assertEqual(A.SOURCE_STRIDE,0x20)
        self.assertEqual(A.EDGES['table_transition_pause'],(0x580E1,0x584A0))
        self.assertEqual(A.EDGES['table_transition_resume'],(0x58151,0x58470))
        self.assertEqual(A.EDGES['script_pause_to_sound_table'],(0x536ED,0x580B0))
        self.assertEqual(A.EDGES['script_resume_to_sound_table'],(0x5371D,0x58110))

    def test_projected_two_transition_and_ordinary_sounds_separate(self):
        values=records()
        values[4]=A.Entry(1,0,0,1,123)
        values[5]=A.Entry(1,0,0,1,123)
        values[6]=A.Entry(1,0,0,0,124)
        result=A.projected_pause(tuple(values))
        self.assertEqual(result['candidate_count'],3)
        self.assertEqual(result['transition_candidate_indices'],[4,5])
        self.assertEqual(result['plain_sound_candidate_indices'],[6])
        self.assertFalse(result['native_adpcm_handles_identical_proven'])
        self.assertFalse(result['permission_to_skip_native_pause'])

    def test_depth_or_field08_ineligible_entry_not_predicted(self):
        values=records()
        values[4]=A.Entry(1,1,0,1,123)
        values[5]=A.Entry(1,0,1,1,123)
        self.assertEqual(A.projected_pause(tuple(values))['candidate_count'],0)

    def test_one_candidate_does_not_justify_suppressing_native_pause(self):
        values=records()
        values[4]=A.Entry(1,0,0,1,123)
        result=A.projected_pause(tuple(values))
        self.assertEqual(result['transition_candidate_indices'],[4])
        self.assertFalse(result['permission_to_skip_native_pause'])

    def test_invalid_or_partial_table_rejected_fail_closed(self):
        values=records()
        with self.assertRaisesRegex(ValueError,'incomplete'):
            A.projected_pause(tuple(values[:-1]))
        values[4]=A.Entry(True,0,0,1,1)
        with self.assertRaisesRegex(ValueError,'invalid'):
            A.projected_pause(tuple(values))
        values[4]=A.Entry(-1,0,0,1,1)
        with self.assertRaisesRegex(ValueError,'invalid'):
            A.projected_pause(tuple(values))

    def test_no_original_dll_calls_memory_writes_or_inferred_epoch(self):
        source=(Path(__file__).resolve().parents[1]/
                'tools/audit_original_cd_esc_sound_table.py').read_text(encoding='utf-8')
        for forbidden in ('GetProcAddress(', 'LoadLibrary(', 'WriteProcessMemory(',
                          'ReadProcessMemory(', 'VirtualProtect(', 'ctypes.',
                          'subprocess.', '.write_bytes('):
            self.assertNotIn(forbidden,source)
        row=records()
        row[3]=A.Entry(1,0,0,1,55)
        assert not A.projected_pause(tuple(row))['native_music_generation_continuity_proven']

if __name__=='__main__':
    unittest.main()
