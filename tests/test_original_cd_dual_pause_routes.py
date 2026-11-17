"""Synthetic-only checks for the original CD direct+Theme dual dispatch."""
import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_original_cd_dual_pause_routes as A

FAKE=b'Synthetic no-original-source-data dual-route PE mock'
class FakePE:
    machine=0x14c
    timestamp=A.ORIGINAL_PE_TIMESTAMP
    image_base=A.ORIGINAL_IMAGE_BASE
    image_size=A.ORIGINAL_IMAGE_SIZE
    def __init__(self,data):
        self.code=bytearray(b'\x90'*0xA5000)
        for rva,raw in A.ANCHORS.values():
            raw=bytes.fromhex(raw)
            self.code[rva:rva+len(raw)]=raw
        for name,at in (
            ('pause',A.GENERIC_PAUSE_TABLE_RVA),
            ('resume',A.GENERIC_RESUME_TABLE_RVA)):
            target=(A.EXPECTED_PAUSE_ROUTE_RVA if name=='pause'
                    else A.EXPECTED_RESUME_ROUTE_RVA)
            pos=at+A.THEME_TYPE*A.TYPE_STRIDE
            self.code[pos:pos+4]=(target+self.image_base).to_bytes(4,'little')
    def read_rva(self,rva,size):
        if rva<0 or size<0 or rva+size>len(self.code):
            raise A.PEFormatError('out of PE bounds')
        return bytes(self.code[rva:rva+size])

def synthetic_audit(fake_pe=FakePE):
    with (patch.object(A,'ORIGINAL_SHA256',hashlib.sha256(FAKE).hexdigest()),
          patch.object(A,'PE32',fake_pe)):
        return A.audit(FAKE)

class OriginalRoutesTests(unittest.TestCase):
    def test_exact_route_count_and_no_authorization(self):
        r=synthetic_audit()
        self.assertEqual(r['opcode_windows_verified'],len(A.ANCHORS))
        self.assertEqual(r['direct_call_edges_verified'],len(A.EDGES))
        self.assertTrue(r['same_native_callback_target_possible'])
        self.assertTrue(r['one_direct_alias_and_two_callback_events_compatible'])
        self.assertFalse(r['two_source_entries_actually_active_at_one_esc_proven'])
        self.assertFalse(r['same_physical_voice_generation_proven'])
        self.assertFalse(r['safe_to_suppress_native_pause'])
        self.assertFalse(r['native_code_or_calls_modified'])

    def test_vtable_stride_addresses_are_exact(self):
        self.assertEqual(A.GENERIC_PAUSE_TABLE_RVA+11*24,0xA3CB0)
        self.assertEqual(A.GENERIC_RESUME_TABLE_RVA+11*24,0xA3CB4)

    def test_sha_gate_precedes_pe_parse(self):
        with patch.object(A,'PE32') as pe:
            with self.assertRaisesRegex(A.PEFormatError,'SHA-256'):
                A.audit(b'wrong original')
            pe.assert_not_called()

    def test_bad_theme_pause_function_pointer_is_rejected(self):
        class Broken(FakePE):
            def __init__(self,raw):
                super().__init__(raw)
                pos=A.GENERIC_PAUSE_TABLE_RVA+11*A.TYPE_STRIDE
                self.code[pos:pos+4]=(0x454d00).to_bytes(4,'little')
        with self.assertRaisesRegex(A.PEFormatError,'Theme-11 pause'):
            synthetic_audit(Broken)

    def test_bad_theme_resume_pointer_is_rejected(self):
        class Broken(FakePE):
            def __init__(self,raw):
                super().__init__(raw)
                pos=A.GENERIC_RESUME_TABLE_RVA+11*A.TYPE_STRIDE
                self.code[pos:pos+4]=(0x454d00).to_bytes(4,'little')
        with self.assertRaisesRegex(A.PEFormatError,'Theme-11 resume'):
            synthetic_audit(Broken)

    def test_bad_generic_dispatch_is_rejected(self):
        class Broken(FakePE):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x58080]=0x90
        with self.assertRaisesRegex(A.PEFormatError,'generic_pause_vtable_dispatch'):
            synthetic_audit(Broken)

    def test_bad_theme_inner_call_is_rejected(self):
        class Broken(FakePE):
            def __init__(self,raw):
                super().__init__(raw)
                self.code[0x59C4E]=0x90
        with self.assertRaisesRegex(A.PEFormatError,'theme11_to_inner_pause'):
            synthetic_audit(Broken)

    def test_no_dynamic_native_calls_or_writes(self):
        source=(Path(__file__).resolve().parents[1]/
                'tools/audit_original_cd_dual_pause_routes.py'
                ).read_text(encoding='utf-8')
        for forbidden in ('ctypes.', 'WriteProcessMemory(', 'ReadProcessMemory(',
                          'LoadLibrary(', 'GetProcAddress(', '.write_bytes(',
                          'subprocess.'):
            self.assertNotIn(forbidden,source)

if __name__=='__main__':
    unittest.main()
