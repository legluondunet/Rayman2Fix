"""Synthetic-only PE audit tests for the 1999 game frame-flip wrapper."""
import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_original_cd_frame_flip as F

FAKE=b'Frame flip synthetic fixture only; no original game data'
class MockPE:
    machine=0x14c
    timestamp=F.ORIGINAL_PE_TIMESTAMP
    image_base=F.ORIGINAL_IMAGE_BASE
    image_size=F.ORIGINAL_IMAGE_SIZE
    def __init__(self,data):
        self.bytes=bytearray(b'\x90'*0xA2000)
        for at,expected in F.ROUTE_INSTRUCTIONS.values():
            raw=bytes.fromhex(expected)
            self.bytes[at:at+len(raw)]=raw
        for at in F.NATIVE_FLIP_CALLERS:
            rel=F.FRAME_FLIP_ENTRY_RVA-at-5
            self.bytes[at:at+5]=b'\xe8'+rel.to_bytes(4,'little',signed=True)
        self.bytes[F.SYNC_FLIP_SYMBOL_RVA:
                   F.SYNC_FLIP_SYMBOL_RVA+len(F.SYNC_FLIP_SYMBOL)]=F.SYNC_FLIP_SYMBOL
    def read_rva(self,rva,length):
        if rva<0 or length<0 or rva+length>len(self.bytes):
            raise F.PEFormatError('fixture PE out of bounds')
        return bytes(self.bytes[rva:rva+length])

def positive(pe=MockPE):
    with (patch.object(F,'ORIGINAL_SHA256',hashlib.sha256(FAKE).hexdigest()),
          patch.object(F,'PE32',pe)):
        return F.audit(FAKE)

class FlipTests(unittest.TestCase):
    def test_fingerprint_positive_no_audio_authorization(self):
        result=positive()
        self.assertEqual(result['verified_original_callers'],4)
        self.assertEqual(result['opcode_sites_verified'],8)
        self.assertTrue(result['original_driver_is_forwarded'])
        self.assertFalse(result['live_frame_cadence_during_jail_20_proven'])
        self.assertFalse(result['runtime_thread_identity_proven'])
        self.assertFalse(result['physical_audio_changes_authorized'])
        self.assertFalse(result['original_game_code_modified'])

    def test_bad_executable_rejected_before_pe(self):
        with patch.object(F,'PE32') as pe:
            with self.assertRaisesRegex(F.PEFormatError,'SHA-256'):
                F.audit(b'not the original')
            pe.assert_not_called()

    def test_mismatched_entry_byte_rejected(self):
        class Bad(MockPE):
            def __init__(self,data):
                super().__init__(data)
                self.bytes[F.FRAME_FLIP_ENTRY_RVA]=0x90
        with self.assertRaisesRegex(F.PEFormatError,'frame_flip_start'):
            positive(Bad)

    def test_mismatched_original_driver_slot_rejected(self):
        class Bad(MockPE):
            def __init__(self,data):
                super().__init__(data)
                self.bytes[F.DRIVER_FLIP_CALL_RVA+2]=0x18
        with self.assertRaisesRegex(F.PEFormatError,'native_driver_sync_flip'):
            positive(Bad)

    def test_modified_cinematic_call_target_rejected(self):
        class Bad(MockPE):
            def __init__(self,data):
                super().__init__(data)
                self.bytes[0x5EF87+2]=0x00
        with self.assertRaisesRegex(F.PEFormatError,'cinematic_flip_caller_two'):
            positive(Bad)

    def test_resolver_name_changed_rejected(self):
        class Bad(MockPE):
            def __init__(self,data):
                super().__init__(data)
                self.bytes[F.SYNC_FLIP_SYMBOL_RVA]=0x00
        with self.assertRaisesRegex(F.PEFormatError,'symbol'):
            positive(Bad)

    def test_no_imports_loaders_or_binary_output(self):
        script=(Path(__file__).resolve().parents[1]/
                'tools/audit_original_cd_frame_flip.py').read_text()
        for prohibited in ('ctypes.', 'LoadLibrary(', 'WriteProcessMemory(',
                           'ReadProcessMemory(', '.write_bytes(',
                           'subprocess.'):
            self.assertNotIn(prohibited,script)

if __name__=='__main__':
    unittest.main()
