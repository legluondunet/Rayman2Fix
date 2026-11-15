"""No original game bytes: fail-closed synthetic CD controller handoff tests."""
from __future__ import annotations
import hashlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_original_theme_handoff as A


class FakeCdPE:
    machine=0x14C
    magic=0x10B
    timestamp=A.ORIGINAL_TIMESTAMP
    image_base=0x400000
    image_size=A.ORIGINAL_IMAGE_SIZE
    def __init__(self, raw):
        self.text=bytearray(bytes([0x90])*0x9A200)
        for rva,hexcode in A.ANCHORS.values():
            b=bytes.fromhex(hexcode)
            off=rva-0x1000
            self.text[off:off+len(b)]=b
    def read_rva(self,rva,n):
        off=rva-0x1000
        if off<0 or n<0 or off+n>len(self.text):
            raise A.OriginalPEError('synthetic CD code bounds')
        return bytes(self.text[off:off+n])


def synthetic_audit(source=b'synthetic CD only',cls=FakeCdPE):
    with (patch.object(A,'ORIGINAL_CD_SHA256',hashlib.sha256(source).hexdigest()),
          patch.object(A,'ORIGINAL_SIZE',len(source)),
          patch.object(A,'PE32',cls)):
        return A.audit(source)


class OriginalControllerTests(unittest.TestCase):
    def test_exact_synthetic_original_switch_structure(self):
        out=synthetic_audit()
        self.assertEqual(out['verified_opcode_windows'],16)
        self.assertEqual(out['verified_direct_call_edges'],5)
        self.assertTrue(out['original_pending_then_current_stage_proven_statically'])
        self.assertEqual(out['pending_flag_write_va'],'0x4595F6')
        self.assertEqual(out['pending_target_write_va'],'0x4595FC')
        self.assertEqual(out['controller_flag_clear_va'],'0x459476')
        self.assertEqual(out['controller_current_key_commit_va'],'0x45948E')
        self.assertFalse(out['native_audio_epoch_stable_across_handoff_proven'])
        self.assertFalse(out['audio_change_authorized'])
        self.assertFalse(out['observed_esc_pause_during_handoff_proven'])

    def test_bad_sha_fails_before_pe_parse(self):
        with patch.object(A,'PE32') as fake:
            with self.assertRaisesRegex(A.OriginalPEError,'SHA-256'):
                A.audit(b'an unrelated GOG executable')
            fake.assert_not_called()

    def test_wrong_cd_pe_metadata_refused(self):
        class Bad(FakeCdPE):
            timestamp=0
        with self.assertRaisesRegex(A.OriginalPEError,'metadata'):
            synthetic_audit(cls=Bad)

    def test_unexpected_pending_target_write_refused(self):
        class Bad(FakeCdPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x595FE-0x1000]=0x48
        with self.assertRaisesRegex(A.OriginalPEError,'pending_target'):
            synthetic_audit(cls=Bad)

    def test_missing_switch_flag_clear_refused(self):
        class Bad(FakeCdPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x59476-0x1000]=0x90
        with self.assertRaisesRegex(A.OriginalPEError,'pending_transfer_sequence|clears_switch'):
            synthetic_audit(cls=Bad)

    def test_changed_new_current_key_commit_refused(self):
        class Bad(FakeCdPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x59490-0x1000]=0x44
        with self.assertRaisesRegex(A.OriginalPEError,'commits_new_current'):
            synthetic_audit(cls=Bad)

    def test_direct_pending_arming_edge_verified_and_mutation_refused(self):
        self.assertEqual(A.CALL_EDGES['theme_constructor_to_pending_arming'],
                         (0x599D5,0x595D0))
        class Bad(FakeCdPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x599D6-0x1000]=0
        with self.assertRaisesRegex(A.OriginalPEError,'theme_constructor'):
            synthetic_audit(cls=Bad)

    def test_controller_callback_registered_as_function_pointer(self):
        self.assertEqual(A.EXPECTED_CONTROLLER_CALLBACK_VA,0x4596E0)
        self.assertEqual(A.CALL_EDGES['theme_constructor_registers_update_callback'],
                         (0x59AEC,0x58220))
        class Bad(FakeCdPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x59AE6-0x1000]=0x70
        with self.assertRaisesRegex(A.OriginalPEError,'register_controller_update_pointer|registration'):
            synthetic_audit(cls=Bad)

    def test_reader_uses_real_original_current_descriptor_address(self):
        rva,bytestr=A.ANCHORS['compare_player_ref_against_current']
        self.assertEqual(rva,0x5997B)
        self.assertEqual(bytes.fromhex(bytestr),bytes.fromhex('3b 1d cc 95 4b 00'))
        self.assertEqual(A.CALL_EDGES['game_audio_switch_to_theme_constructor'],
                         (0x57F38,0x59950))

    def test_no_observer_hooks_or_audio_permissions_in_auditor(self):
        src=(Path(__file__).resolve().parents[1]/
             'tools/audit_original_theme_handoff.py').read_text(encoding='utf-8')
        for forbidden in ('GetProcAddress(', 'LoadLibrary(', 'WriteProcessMemory(',
                          'VirtualProtect(', 'ctypes.', 'subprocess.', '.write_bytes('):
            self.assertNotIn(forbidden,src)
        self.assertNotIn('true_audio_generation_proven',str(synthetic_audit()))


if __name__=='__main__':
    unittest.main()
