"""Synthetic-only tests of original APMMX mixer completion path.

No proprietary DLL bytes are embedded, executed, or written.
"""
from __future__ import annotations
import hashlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_adpcm_buffer_fill as A


class SyntheticMixerPE:
    machine = 0x14C
    timestamp = 0x38076F98
    image_base = 0x10000000
    image_size = 0xEE000

    def __init__(self, raw):
        self.code = bytearray(b'\x90' * 0xD800)
        for rva, hex_bytes in A.ANCHORS.values():
            op = bytes.fromhex(hex_bytes)
            self.code[rva-0x1000:rva-0x1000+len(op)] = op

    def read_rva(self, rva, n):
        if rva==0x12010:
            return b'_SND_fn_lCreateNewBufferExSxd@16\0'[:n]
        i = rva-0x1000
        if i < 0 or n < 0 or i+n > len(self.code):
            raise A.PEFormatError('synthetic PE bounds')
        return bytes(self.code[i:i+n])


class MixerPathTests(unittest.TestCase):
    def test_complete_synthetic_mixer_path(self):
        fixture=b'synthetic original mixer shape only'
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', SyntheticMixerPE)):
            report=A.audit(fixture)
        self.assertEqual(report['verified_opcode_windows'],27)
        self.assertEqual(report['verified_direct_call_edges'],10)
        self.assertEqual(report['verified_conditional_branches'],3)
        self.assertTrue(report['normal_buffer_fill_can_mark_voice_inactive'])
        self.assertTrue(
            report['original_two_callbacks_registered_through_indirect_output_interface'])
        self.assertFalse(report['actual_callback_execution_during_esc_observed'])
        self.assertFalse(report['registered_callbacks_always_run_on_a_separate_thread_proven'])
        self.assertEqual(report['dynamic_output_provider_symbol'],
                         '_SND_fn_lCreateNewBufferExSxd@16')
        self.assertTrue(report['dynamic_output_provider_resolution_proven'])
        self.assertFalse(report['specific_runtime_provider_module_observed'])
        self.assertTrue(report['mixer_wrappers_hold_known_driver_lock'])
        self.assertFalse(report['getpos_testisplaying_stop_required_for_this_write'])
        self.assertFalse(report['occurred_during_owners_first_esc_proven'])
        self.assertFalse(report['native_retirement_or_new_physical_generation_proven'])
        self.assertFalse(report['permission_to_hook_or_change_audio'])

    def test_other_dll_refused_before_pe_parse(self):
        with patch.object(A, 'PE32') as ctor:
            with self.assertRaisesRegex(A.PEFormatError, 'SHA-256 mismatch'):
                A.audit(b'unknown version')
            ctor.assert_not_called()

    def test_bad_pe_metadata_is_rejected(self):
        fixture=b'bad synthetic metadata'
        class Bad(SyntheticMixerPE):
            image_size=0x11111
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'metadata'):
                A.audit(fixture)

    def test_original_active_zero_write_is_guarded(self):
        fixture=b'bad synthetic native write'
        class Bad(SyntheticMixerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.code[0x17CA-0x1000+2]=0x2C
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'progress_inactive_write'):
                A.audit(fixture)

    def test_broken_mixer_call_target_fails_closed(self):
        fixture=b'bad mixer call target'
        class Bad(SyntheticMixerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.code[0x34F2-0x1000+1]=0x00
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'mixer_second_fill'):
                A.audit(fixture)

    def test_broken_guard_branch_fails_closed(self):
        fixture=b'bad zero flag branch'
        class Bad(SyntheticMixerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.code[0x17C4-0x1000+2]=0x00
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'progress_field20_gate|conditional'):
                A.audit(fixture)

    def test_mixer_lock_release_must_remain(self):
        fixture=b'bad native mixer unlock'
        class Bad(SyntheticMixerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.code[0x353E-0x1000]=0x90
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'mixer_second_unlock'):
                A.audit(fixture)

    def test_original_output_registration_binds_two_distinct_mix_callbacks(self):
        self.assertEqual(A.EDGES['first_register_callback'], (0x2B41,0x3A50))
        self.assertEqual(A.EDGES['second_register_callback'], (0x2B74,0x3A50))
        self.assertEqual(bytes.fromhex(
            A.ANCHORS['first_callback_registration_address'][1])[-4:],
                         (0x10003400).to_bytes(4,'little'))
        self.assertEqual(bytes.fromhex(
            A.ANCHORS['second_callback_registration_address'][1])[-4:],
                         (0x100034B0).to_bytes(4,'little'))
        self.assertIn('ff 15',A.ANCHORS['native_callback_registration_indirect_call'][1])
        # Even two registrations never prove the host thread or timing.

    def test_mutated_output_registration_pointer_refused(self):
        fixture=b'synthetic bad callback function ptr'
        class Bad(SyntheticMixerPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x2B3D-0x1000]=0x10
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'first_callback_registration_address'):
                A.audit(fixture)

    def test_removed_indirect_registration_call_refused(self):
        fixture=b'synthetic no external callback registration'
        class Bad(SyntheticMixerPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x3A73-0x1000]=0x90
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'native_callback_registration_indirect_call'):
                A.audit(fixture)

    def test_output_provider_indirect_import_is_expected_original_symbol(self):
        self.assertEqual(A.EDGES['output_provider_lookup'],(0x4B83,0x3B00))
        self.assertEqual(A.ANCHORS['output_provider_fallback_returns_error'],
                         (0x3A80, '83 c8 ff c2 10 00'))
        self.assertEqual(bytes.fromhex(
            A.ANCHORS['output_provider_symbol_push'][1])[-4:],
            (0x10012010).to_bytes(4,'little'))

    def test_modified_original_output_provider_name_refused(self):
        fixture=b'synthetic invalid backend API name'
        class Bad(SyntheticMixerPE):
            def read_rva(self,rva,n):
                if rva==0x12010:
                    return b'X'*n
                return super().read_rva(rva,n)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'provider dynamic symbol'):
                A.audit(fixture)

    def test_output_provider_resolver_must_match_original(self):
        fixture=b'synthetic bad output resolver'
        class Bad(SyntheticMixerPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x4B83-0x1000]=0x90
            def read_rva(self,rva,n):
                if rva==0x12010:
                    return b'_SND_fn_lCreateNewBufferExSxd@16\0'
                return super().read_rva(rva,n)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256',
                           hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'output_provider_resolver_call'):
                A.audit(fixture)

    def test_progress_conditional_must_have_all_three_zero_gates(self):
        self.assertTrue(A.model_progress(
            field24_zero=True,field_d8_zero=True,field20_zero=True
        ).activity_may_be_cleared)
        self.assertFalse(A.model_progress(
            field24_zero=True,field_d8_zero=True,field20_zero=True
        ).physical_retirement_proven)
        for key in ('field24_zero','field_d8_zero','field20_zero'):
            values=dict(field24_zero=True,field_d8_zero=True,field20_zero=True)
            values[key]=False
            with self.subTest(key=key):
                self.assertFalse(A.model_progress(**values).activity_may_be_cleared)

    def test_unknown_gate_must_not_be_treated_as_zero(self):
        for bad in (None,0,1,'true',-1):
            with self.subTest(value=bad):
                with self.assertRaises(ValueError):
                    A.model_progress(field24_zero=True,
                                     field_d8_zero=bad,field20_zero=True)

    def test_no_execution_probe_or_physical_retirement_claim(self):
        source=(Path(__file__).resolve().parents[1]/
                'tools/audit_adpcm_buffer_fill.py').read_text(encoding='utf-8')
        for forbidden in ('GetProcAddress(', 'LoadLibrary(', 'ctypes',
                          'WriteProcessMemory(', 'subprocess.', '.write_bytes('):
            self.assertNotIn(forbidden,source)
        self.assertEqual(A.EDGES['progress_through_buffer_fill'],
                         (0x1AD1,0x1740))
        self.assertEqual(A.EDGES['mixer_first_fill'],(0x344C,0x18D0))
        self.assertEqual(A.EDGES['mixer_second_fill'],(0x34F2,0x18D0))


if __name__ == '__main__':
    unittest.main()
