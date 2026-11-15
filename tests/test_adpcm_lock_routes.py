"""Synthetic-only checks for the original driver lock/cleanup call route.

No proprietary DLL bytes or process execution are present in fixtures.
"""
from __future__ import annotations

import hashlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_adpcm_lock_routes as A


class FakeOwnerPE:
    machine = 0x14C
    timestamp = 0x38076F98
    image_base = 0x10000000
    image_size = 0xEE000

    def __init__(self, raw):
        self.text = bytearray(b'\x90' * 0xD800)
        for site, hex_bytes in A.ANCHORS.values():
            content = bytes.fromhex(hex_bytes)
            self.text[site-0x1000:site-0x1000+len(content)] = content

    def exports(self):
        return {'_SND_fn_bTestIsPlayingSxdADPCM@4': 0x5150}

    def read_rva(self, rva, size):
        start = rva-0x1000
        if start < 0 or size < 0 or start + size > len(self.text):
            raise A.PEFormatError('synthetic .text bounds exceeded')
        return bytes(self.text[start:start+size])


class FakeWavPE:
    machine = 0x14C
    timestamp = 0x38076FB4
    image_base = 0x10000000
    image_size = 0x87000

    def __init__(self, raw):
        self.text = bytearray(b'\x90' * 0x22000)
        for rva, hex_bytes in A.WAVMW_SITES.values():
            opcodes = bytes.fromhex(hex_bytes)
            self.text[rva:rva+len(opcodes)] = opcodes
        symbol = b'_SND_fn_bTestIsPlayingSxdADPCM@4\0'
        self.text[0x21560:0x21560+len(symbol)] = symbol

    def read_rva(self, rva, size):
        if rva < 0 or size < 0 or rva+size > len(self.text):
            raise A.PEFormatError('WAVMW synthetic bounds')
        return bytes(self.text[rva:rva+size])

    def exports(self):
        return {'_SND_fn_bTestIsPlayingSxd@4': 0xB8A0}


class LockRoutesTests(unittest.TestCase):
    def test_exact_synthetic_anchor_and_relative_edges(self):
        fixture = b'not original copyright bytes'
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', FakeOwnerPE)):
            result = A.audit(fixture)
        self.assertEqual(result['opcode_anchors_verified'], 33)
        self.assertEqual(result['relative_call_edges_verified'], 24)
        self.assertEqual(result['local_lockless_ranges_verified'], 7)
        self.assertTrue(result['position_query_has_no_direct_original_driver_lock_calls'])
        self.assertFalse(result['external_or_indirect_serialization_proven_absent'])
        self.assertEqual(len(result['known_locked_routes']), 6)
        self.assertFalse(result['all_retirement_synchronized_proven'])
        self.assertTrue(result['exported_stop_count_cannot_prove_no_retirement'])
        self.assertTrue(result['setparam_may_retire_inactive_without_exported_stop'])
        self.assertTrue(result['setparam_both_routes_use_known_driver_lock'])
        self.assertFalse(result['setparam_currently_observed_in_owner_esc_log'])
        self.assertFalse(result['runtime_audio_change_authorized'])

    def test_wrong_sha_blocks_parse(self):
        with patch.object(A, 'PE32') as ctor:
            with self.assertRaisesRegex(A.PEFormatError, 'SHA-256 mismatch'):
                A.audit(b'not the owner file')
            ctor.assert_not_called()

    def test_wrong_pe_metadata_blocks_claim(self):
        fixture = b'wrong time stamp'
        class Bad(FakeOwnerPE):
            timestamp = 0
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'PE metadata mismatch'):
                A.audit(fixture)

    def test_opcode_mutation_blocks_claim(self):
        fixture = b'mutated direct call'
        class Bad(FakeOwnerPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x1F68-0x1000] = 0x90
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'cleanup_can_retire'):
                A.audit(fixture)

    def test_setparam_original_both_export_calls_use_driver_lock(self):
        self.assertEqual(A.EDGES['setparam_sxd_export_call'],(0x513C,0x28B0))
        self.assertEqual(A.EDGES['setparam_transition_export_call'],(0x544C,0x3250))
        self.assertEqual(A.EDGES['setparam_sxd_lock'],(0x28C1,0x1020))
        self.assertEqual(A.EDGES['setparam_sxd_inner'],(0x28D3,0x1C30))
        self.assertEqual(A.EDGES['setparam_sxd_unlock'],(0x28E9,0x1030))
        self.assertEqual(A.EDGES['setparam_transition_lock'],(0x326A,0x1020))
        self.assertEqual(A.EDGES['setparam_transition_inner'],(0x3278,0x1C30))
        self.assertEqual(A.EDGES['setparam_transition_unlock'],(0x328C,0x1030))
        self.assertEqual(A.EDGES['setparam_inner_retire'],(0x1C8D,0x15D0))
        self.assertIn('setparam_sxd',A.LOCKED_ROUTES)
        self.assertIn('setparam_transition',A.LOCKED_ROUTES)

    def test_mutated_setparam_inner_active_gate_fails_closed(self):
        fixture=b'synthetic SetParam active conditional mutation'
        class Bad(FakeOwnerPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x1C59-0x1000]=0x90
        with (patch.object(A,'OWNER_ORIGINAL_SHA256',hashlib.sha256(fixture).hexdigest()),
              patch.object(A,'PE32',Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'setparam_inner_tests_active'):
                A.audit(fixture)

    def test_mutated_setparam_native_lock_release_fails_closed(self):
        fixture=b'synthetic SetParam lock release corrupted'
        class Bad(FakeOwnerPE):
            def __init__(self,raw):
                super().__init__(raw)
                self.text[0x328C-0x1000]=0x90
        with (patch.object(A,'OWNER_ORIGINAL_SHA256',hashlib.sha256(fixture).hexdigest()),
              patch.object(A,'PE32',Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'setparam_transition_unlock'):
                A.audit(fixture)

    def test_new_direct_lock_call_in_cleanup_path_blocks_claim(self):
        fixture = b'inserted lock call in synthetic cleanup block'
        class Bad(FakeOwnerPE):
            def __init__(self, raw):
                super().__init__(raw)
                site = 0x291A
                self.text[site-0x1000] = 0xE8
                struct.pack_into('<i', self.text, site-0x1000+1, 0x1020-(site+5))
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'unexpected direct lock'):
                A.audit(fixture)

    def test_getpos_reader_direct_lock_call_would_invalidate_original_claim(self):
        fixture = b'synthetic GetPos lock call inserted'
        class Bad(FakeOwnerPE):
            def __init__(self, raw):
                super().__init__(raw)
                site=0x2380
                self.text[site-0x1000]=0xE8
                struct.pack_into('<i',self.text,site-0x1000+1,
                                 0x1020-(site+5))
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError,
                                        'unexpected direct lock'):
                A.audit(fixture)

    def test_getpos_local_lockless_ranges_do_not_prove_no_external_lock(self):
        self.assertIn((0x51A0,0x51B0),A.NO_LOCAL_LOCK)
        self.assertIn((0x2A00,0x2A55),A.NO_LOCAL_LOCK)
        self.assertIn((0x2320,0x247E),A.NO_LOCAL_LOCK)
        self.assertNotIn((0x2780,0x283D),A.NO_LOCAL_LOCK)
        self.assertNotIn((0x3340,0x3371),A.NO_LOCAL_LOCK)

    def test_extra_known_lock_entry_in_play_route_blocks_claim(self):
        fixture = b'extra lock call in synthetic play function'
        class Bad(FakeOwnerPE):
            def __init__(self, raw):
                super().__init__(raw)
                site = 0x27E8
                self.text[site-0x1000] = 0xE8
                struct.pack_into('<i', self.text, site-0x1000+1, 0x1020-(site+5))
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(fixture).hexdigest()),
              patch.object(A, 'PE32', Bad)):
            with self.assertRaisesRegex(A.PEFormatError, 'lock envelope mismatch'):
                A.audit(fixture)

    def test_raw_signed_call_target_and_no_implicit_reachability(self):
        code = bytearray(b'\x90' * 18)
        code[0] = 0xE8
        struct.pack_into('<i', code, 1, 0x2000-(0x1000+5))
        code[8] = 0xE8
        struct.pack_into('<i', code, 9, 0x1000-(0x1008+5))
        self.assertEqual(A.direct_rel32_candidates(code, 0x1000, 0x2000), (0x1000,))
        self.assertEqual(A.direct_rel32_candidates(code, 0x1000, 0x1000), (0x1008,))
        self.assertEqual(A.direct_rel32_candidates(code[:4], 0x1000, 0x2000), ())

    def test_known_retire_chain_not_an_exported_stop_call(self):
        self.assertEqual(A.EDGES['export_test_is_playing'], (0x5155, 0x2900))
        self.assertEqual(A.EDGES['test_calls_cleanup'], (0x290E, 0x1F50))
        self.assertEqual(A.EDGES['cleanup_can_retire'], (0x1F68, 0x15D0))
        self.assertNotIn('test_is_playing', A.LOCKED_ROUTES)
        self.assertIn((0x5150, 0x515F), A.NO_LOCAL_LOCK)
        self.assertIn((0x2900, 0x2923), A.NO_LOCAL_LOCK)
        self.assertIn((0x1F50, 0x1F73), A.NO_LOCAL_LOCK)
        self.assertIn((0x15D0, 0x16F1), A.NO_LOCAL_LOCK)

    def test_two_original_dlls_link_type4_to_testplaying_delegate(self):
        primary, wave = b'fake original APMMX', b'fake original WAVMW'
        def factory(blob):
            return FakeOwnerPE(blob) if blob == primary else FakeWavPE(blob)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(primary).hexdigest()),
              patch.object(A, 'WAVMW_OWNER_SHA256', hashlib.sha256(wave).hexdigest()),
              patch.object(A, 'PE32', factory)):
            report = A.audit_wavmw_bridge(wave, primary)
        self.assertTrue(report['static_type4_dispatch_chain_verified'])
        self.assertEqual(report['wavmw_opcode_anchors_verified'], 23)
        self.assertEqual(report['wavmw_direct_edges_verified'], 5)
        self.assertFalse(report['actual_runtime_function_pointer_observed'])
        self.assertTrue(report['query_can_retire_wavmw_inner_on_zero_result'])
        self.assertTrue(report['query_can_unlink_outer_sxd_node_on_zero_result'])
        self.assertFalse(report['permission_to_change_audio'])

    def test_dual_dll_audit_refuses_unapproved_wavmw_before_pe_parse(self):
        primary = b'fake original APMMX'
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(primary).hexdigest()),
              patch.object(A, 'PE32', FakeOwnerPE)):
            with self.assertRaisesRegex(A.PEFormatError, 'WAVMW SHA-256 mismatch'):
                A.audit_wavmw_bridge(b'an unknown DLL', primary)

    def test_dual_dll_audit_rejects_unexpected_wavmw_symbol(self):
        primary, wave = b'fake original APMMX', b'fake original WAVMW'
        class MissingSymbol(FakeWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x21560] = 0
        def factory(blob):
            return FakeOwnerPE(blob) if blob == primary else MissingSymbol(blob)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(primary).hexdigest()),
              patch.object(A, 'WAVMW_OWNER_SHA256', hashlib.sha256(wave).hexdigest()),
              patch.object(A, 'PE32', factory)):
            with self.assertRaisesRegex(A.PEFormatError, 'resolver symbol mismatch'):
                A.audit_wavmw_bridge(wave, primary)

    def test_dual_dll_audit_rejects_changed_dispatch_function_pointer(self):
        primary, wave = b'fake original APMMX', b'fake original WAVMW'
        class MissingDispatch(FakeWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x7BB0] = 0x90
        def factory(blob):
            return FakeOwnerPE(blob) if blob == primary else MissingDispatch(blob)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(primary).hexdigest()),
              patch.object(A, 'WAVMW_OWNER_SHA256', hashlib.sha256(wave).hexdigest()),
              patch.object(A, 'PE32', factory)):
            with self.assertRaisesRegex(A.PEFormatError, 'indirect_test_vtable_call'):
                A.audit_wavmw_bridge(wave, primary)

    def test_dual_dll_audit_rejects_wrong_wavmw_export(self):
        primary, wave = b'fake original APMMX', b'fake original WAVMW'
        class DifferentExport(FakeWavPE):
            def exports(self):
                return {'_SND_fn_bTestIsPlayingSxd@4': 0xB900}
        def factory(blob):
            return FakeOwnerPE(blob) if blob == primary else DifferentExport(blob)
        with (patch.object(A, 'OWNER_ORIGINAL_SHA256', hashlib.sha256(primary).hexdigest()),
              patch.object(A, 'WAVMW_OWNER_SHA256', hashlib.sha256(wave).hexdigest()),
              patch.object(A, 'PE32', factory)):
            with self.assertRaisesRegex(A.PEFormatError, 'WAVMW TestIsPlaying export mismatch'):
                A.audit_wavmw_bridge(wave, primary)

    def test_static_auditor_makes_no_native_runtime_writes(self):
        source = (Path(__file__).resolve().parents[1] /
                  'tools/audit_adpcm_lock_routes.py').read_text(encoding='utf-8')
        for prohibited in ('ctypes', 'subprocess', 'LoadLibrary', 'WriteProcessMemory',
                           'VirtualProtect', '.write_bytes(', 'os.system('):
            self.assertNotIn(prohibited, source)


if __name__ == '__main__':
    unittest.main()
