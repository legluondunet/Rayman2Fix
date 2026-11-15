"""Synthetic-only original Theme callback registration and guard audit."""
from __future__ import annotations
import hashlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_original_cd_theme_callback as A


class FakeOriginalCD:
    machine=0x14C
    timestamp=A.ORIGINAL_PE_TIMESTAMP
    image_base=A.ORIGINAL_IMAGE_BASE
    image_size=A.ORIGINAL_IMAGE_SIZE

    def __init__(self,data):
        self.text=bytearray(bytes([0x90])*0xA1000)
        for rva,hex_bytes in A.SITES.values():
            op=bytes.fromhex(hex_bytes)
            self.text[rva:rva+len(op)]=op
        for site,dst in A.DIRECT.values():
            self.text[site]=0xE8
            struct.pack_into('<i',self.text,site+1,dst-(site+5))
        for site,dst,op in A.SHORT_BRANCHES.values():
            self.text[site]=op
            self.text[site+1]=(dst-(site+2))&0xFF

    def read_rva(self,rva,n):
        if rva<0 or n<0 or rva+n>len(self.text):
            raise A.PEFormatError('fake original CD PE bounds')
        return bytes(self.text[rva:rva+n])


def synthetic_audit(pe=FakeOriginalCD):
    data=b'non-proprietary synthetic CD callback fixture'
    with (patch.object(A,'ORIGINAL_SHA256',
                       hashlib.sha256(data).hexdigest()),
          patch.object(A,'PE32',pe)):
        return A.audit(data)


class OriginalThemeCallbackTests(unittest.TestCase):
    def test_original_cd_callback_path_is_complete_but_not_live_evidence(self):
        report=synthetic_audit()
        self.assertTrue(report['original_pe_verified'])
        self.assertEqual(report['verified_opcodes'],16)
        self.assertEqual(report['verified_direct_call_edges'],4)
        self.assertEqual(report['verified_conditional_branches'],4)
        self.assertEqual(report['callback_entry_va'],'0x004596E0')
        self.assertEqual(report['callback_controller_va'],'0x004B95B8')
        self.assertTrue(report['pending_target_committed_on_eligible_callback'])
        for name in ('callback_invoked_during_owner_menu_proven',
                     'global_guard_was_nonzero_during_esc_proven',
                     'controller_state_was_nonzero_during_esc_proven',
                     'scheduler_is_original_audio_processing_proven',
                     'music_voice_generation_proven',
                     'four_second_handoff_timer_proven',
                     'runtime_audio_change_authorized'):
            self.assertFalse(report[name],name)

    def test_alternative_untrusted_executable_refused_by_hash(self):
        with patch.object(A,'PE32') as ctor:
            with self.assertRaisesRegex(A.PEFormatError,'SHA-256 mismatch'):
                A.audit(b'not the approved 1999 original')
            ctor.assert_not_called()

    def test_unexpected_original_cd_machine_rejected(self):
        class WrongArch(FakeOriginalCD):
            machine=0x8664
        with self.assertRaisesRegex(A.PEFormatError,'PE header'):
            synthetic_audit(WrongArch)

    def test_callback_pointer_registration_is_immutable_original(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x59AE6]=0
        with self.assertRaisesRegex(A.PEFormatError,
                                    'register_theme_callback_pointer'):
            synthetic_audit(Mutated)

    def test_callback_receiver_is_exact_original_controller(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x59AE1]=0
        with self.assertRaisesRegex(A.PEFormatError,
                                    'register_theme_audio_context'):
            synthetic_audit(Mutated)

    def test_callback_registration_must_call_expected_audio_create(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x59AEC+1]=0
        with self.assertRaisesRegex(A.PEFormatError,
                                    'register_audio_create_call'):
            synthetic_audit(Mutated)

    def test_native_callback_must_reach_pending_update_function(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x59707+1]=0
        with self.assertRaisesRegex(A.PEFormatError,'callback_update_call'):
            synthetic_audit(Mutated)

    def test_global_callback_gate_must_remain(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x596E8+1]=0
        with self.assertRaisesRegex(A.PEFormatError,
                                    'callback_global_gate_conditional'):
            synthetic_audit(Mutated)

    def test_controller_inactive_or_busy_callback_must_skip(self):
        for site in (0x596F1,0x596F8):
            class Mutated(FakeOriginalCD):
                def __init__(self,data):
                    super().__init__(data)
                    self.text[site+1]=0
            with self.subTest(site=hex(site)):
                with self.assertRaisesRegex(A.PEFormatError,
                                            'callback_active_controller_gate|callback_state_gate'):
                    synthetic_audit(Mutated)

    def test_pending_promotion_must_use_original_pointer_slot(self):
        class Mutated(FakeOriginalCD):
            def __init__(self,data):
                super().__init__(data)
                self.text[0x5948E+2]=0x18
        with self.assertRaisesRegex(A.PEFormatError,'update_promotes_target'):
            synthetic_audit(Mutated)

    def test_pending_clear_must_precede_current_promotion(self):
        clear=A.SITES['update_clears_pending'][0]
        promote=A.SITES['update_promotes_target'][0]
        self.assertLess(clear,promote)
        self.assertEqual(A.DIRECT['callback_calls_update'],(0x59707,0x59460))
        self.assertEqual(A.DIRECT['player_create_registers_callback'],
                         (0x59AEC,0x58220))

    def test_no_original_callback_invocation_or_runtime_process_hacks(self):
        src=(Path(__file__).resolve().parents[1]/'tools'/
             'audit_original_cd_theme_callback.py').read_text(encoding='utf8')
        for token in ('ctypes.', 'subprocess.', 'GetProcAddress(',
                      'LoadLibrary(', 'ReadProcessMemory(',
                      'WriteProcessMemory(', 'VirtualProtect('):
            self.assertNotIn(token,src)


if __name__=='__main__':
    unittest.main()
