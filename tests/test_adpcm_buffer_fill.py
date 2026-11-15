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
        self.assertEqual(report['verified_opcode_windows'],16)
        self.assertEqual(report['verified_direct_call_edges'],7)
        self.assertEqual(report['verified_conditional_branches'],3)
        self.assertTrue(report['normal_buffer_fill_can_mark_voice_inactive'])
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
