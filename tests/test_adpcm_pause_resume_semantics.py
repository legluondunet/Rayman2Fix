"""Synthetic-only original ADPCM type-4 Pause/Resume contract tests."""
from __future__ import annotations
import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_adpcm_pause_resume_semantics as A

WAV=b'only synthetic WAVMW test input, no original binary'
APM=b'only synthetic APMMX test input, no original binary'


class FakeWave:
    machine=0x14C
    timestamp=0x38076FB4
    image_base=0x10000000
    image_size=0x87000
    def __init__(self, data):
        self.code=bytearray(bytes([0x90])*0x1E000)
        for site,encoded in A.WAV_SITES.values():
            b=bytes.fromhex(encoded)
            self.code[site:site+len(b)]=b
    def read_rva(self, rva,n):
        if rva<0 or rva+n>len(self.code):
            raise A.PEFormatError('fake WAV bounds')
        return bytes(self.code[rva:rva+n])
    def exports(self):
        return {
            '_SND_fn_vPauseTransitionSxd@4':0xC7E0,
            '_SND_fn_vResumeTransitionSxd@4':0xC830,
        }


class FakeAdpcm:
    machine=0x14C
    timestamp=0x38076F98
    image_base=0x10000000
    image_size=0xEE000
    def __init__(self,data):
        self.code=bytearray(bytes([0x90])*0xD800)
        for site,encoded in A.APM_SITES.values():
            b=bytes.fromhex(encoded)
            self.code[site-0x1000:site-0x1000+len(b)]=b
    def read_rva(self, rva,n):
        off=rva-0x1000
        if off<0 or off+n>len(self.code):
            raise A.PEFormatError('fake APM bounds')
        return bytes(self.code[off:off+n])
    def exports(self):
        return {
            '_SND_fn_vPauseTransitionSxdADPCM@4':0x54D0,
            '_SND_fn_vResumeTransitionSxdADPCM@4':0x54E0,
        }


def synthetic_audit(wave=FakeWave, apm=FakeAdpcm):
    with (patch.object(A,'WAVMW_OWNER_SHA256',hashlib.sha256(WAV).hexdigest()),
          patch.object(A,'OWNER_ORIGINAL_SHA256',hashlib.sha256(APM).hexdigest()),
          patch.object(A,'PE32',lambda data: wave(data) if data==WAV else apm(data))):
        return A.audit(WAV,APM)


class NativePauseResumeTests(unittest.TestCase):
    def test_both_original_type4_routes_have_one_delegate(self):
        result=synthetic_audit()
        self.assertEqual(result['wav_opcode_windows_verified'],10)
        self.assertEqual(result['wav_direct_edges_verified'],2)
        self.assertEqual(result['wav_type4_branches_verified'],2)
        self.assertEqual(result['apm_opcode_windows_verified'],18)
        self.assertEqual(result['apm_direct_edges_verified'],10)
        self.assertTrue(result['wav_type4_pause_calls_one_dynamic_delegate'])
        self.assertTrue(result['wav_type4_resume_calls_one_dynamic_delegate'])
        self.assertTrue(result['apm_valid_record_pause_writes_one'])
        self.assertTrue(
            result['apm_valid_record_resume_writes_zero_without_reading_previous_pause_flag'])
        self.assertTrue(result['native_wrapper_calls_known_driver_lock_and_unlock'])
        self.assertFalse(result['can_skip_two_early_pause_calls_atomically_proven'])
        self.assertFalse(result['resume_on_reused_record_invariant_proven'])
        self.assertFalse(result['safe_to_enable_early_esc_pause_suppression'])
        self.assertTrue(result['no_native_execution_or_hooking'])

    def test_mismatched_original_wave_refused_before_pe_construction(self):
        with patch.object(A,'PE32') as ctor:
            with self.assertRaisesRegex(A.PEFormatError,'wav original SHA-256 mismatch'):
                A.audit(b'not WAVMW', APM)
            ctor.assert_not_called()

    def test_mismatched_original_apm_refused(self):
        with patch.object(A,'PE32') as ctor:
            with self.assertRaisesRegex(A.PEFormatError,'apm original SHA-256 mismatch'):
                A.audit(WAV,b'not APMMX')
            # WAV is parsed first; APM fails before its own PE parse.
            self.assertEqual(ctor.call_count,0)  # WAV cannot pass hash either

    def test_native_resume_zero_write_is_not_optional(self):
        class Bad(FakeAdpcm):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x1F33-0x1000]=0x01
        with self.assertRaisesRegex(A.PEFormatError,'resume_write_zero'):
            synthetic_audit(apm=Bad)

    def test_native_pause_one_write_is_not_optional(self):
        class Bad(FakeAdpcm):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x1EF3-0x1000]=0x00
        with self.assertRaisesRegex(A.PEFormatError,'pause_write_one'):
            synthetic_audit(apm=Bad)

    def test_invalid_handle_guard_still_present(self):
        class Bad(FakeAdpcm):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x1F1D-0x1000]=0x90
        with self.assertRaisesRegex(A.PEFormatError,'resume_validate_handle'):
            synthetic_audit(apm=Bad)

    def test_unknown_original_wav_export_is_rejected(self):
        class Bad(FakeWave):
            def exports(self):
                return {'_SND_fn_vPauseTransitionSxd@4':0xC7E0}
        with self.assertRaisesRegex(A.PEFormatError,'transition export'):
            synthetic_audit(wave=Bad)

    def test_misdirected_wav_type4_resume_is_rejected(self):
        class Bad(FakeWave):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0xC855]=0x00
        with self.assertRaisesRegex(A.PEFormatError,'resume_type4_dispatch|resume_type4_to_trampoline'):
            synthetic_audit(wave=Bad)

    def test_modified_original_driver_lock_path_is_rejected(self):
        class Bad(FakeAdpcm):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x33E8-0x1000]=0x90
        with self.assertRaisesRegex(A.PEFormatError,'resume_wrapper_unlock'):
            synthetic_audit(apm=Bad)

    def test_type4_chooses_correct_parameter_and_single_trampoline(self):
        self.assertEqual(A.WAV_CALLS['pause_type4_to_trampoline'],(0xC804,0x3C60))
        self.assertEqual(A.WAV_CALLS['resume_type4_to_trampoline'],(0xC854,0x3C90))
        self.assertEqual(A.APM_CALLS['pause_wrapper_to_apply'],(0x339B,0x1ED0))
        self.assertEqual(A.APM_CALLS['resume_wrapper_to_apply'],(0x33DB,0x1F10))

    def test_no_in_memory_game_probe_or_audio_mutation(self):
        source=(Path(__file__).resolve().parents[1]/
                'tools/audit_adpcm_pause_resume_semantics.py').read_text(encoding='utf-8')
        for forbidden in ('GetProcAddress(', 'LoadLibrary(', 'WriteProcessMemory(',
                          'VirtualProtect(', 'ctypes.', 'subprocess.', '.write_bytes('):
            self.assertNotIn(forbidden,source)
        self.assertFalse(synthetic_audit()['live_music_voice_generation_proven'])

if __name__=='__main__':
    unittest.main()
