"""Synthetic-only TestIsPlaying branch regressions; no proprietary DLL bytes."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_adpcm_query_outcomes as A
from test_adpcm_lock_routes import FakeOwnerPE, FakeWavPE


class SyntheticQueryApmPE(FakeOwnerPE):
    def __init__(self, raw):
        super().__init__(raw)
        for rva, hex_bytes in tuple(A.APMMX_GATES.values()) + tuple(A.APMMX_POSITION_SITES.values()):
            data = bytes.fromhex(hex_bytes)
            self.text[rva-0x1000:rva-0x1000+len(data)] = data

    def exports(self):
        return {
            '_SND_fn_bTestIsPlayingSxdADPCM@4': 0x5150,
            '_SND_fn_rGetPosSxdADPCM@4': 0x51A0,
        }


class SyntheticQueryWavPE(FakeWavPE):
    def __init__(self, raw):
        super().__init__(raw)
        for rva, hex_bytes in tuple(A.WAVMW_GATES.values()) + tuple(A.WAVMW_GETPOS_SITES.values()):
            data = bytes.fromhex(hex_bytes)
            self.text[rva:rva+len(data)] = data
        symbol = b'_SND_fn_rGetPosSxdADPCM@4\0'
        self.text[0x21608:0x21608+len(symbol)] = symbol

    def exports(self):
        return {
            '_SND_fn_bTestIsPlayingSxd@4': 0xB8A0,
            '_SND_fn_rGetPosSxd@4': 0xBD20,
        }


class OriginalQueryGuardTests(unittest.TestCase):
    def setUp(self):
        self.apm = b'not proprietary ADPCM bytes'
        self.wav = b'not proprietary WAVMW bytes'

    def test_synthetic_complete_branch_audit(self):
        apm, wav = self.apm, self.wav
        def fake_pe(data):
            return SyntheticQueryApmPE(data) if data == apm else SyntheticQueryWavPE(data)
        with (patch.object(A, 'PE32', fake_pe),
              patch.object(A, 'audit_wavmw_bridge',
                           lambda w, a: {'static_type4_dispatch_chain_verified': True})):
            info = A.audit(wav, apm)
        self.assertEqual(info['wavmw_query_gates_verified'], 10)
        self.assertEqual(info['apmmx_query_gates_verified'], 9)
        self.assertEqual(info['conditional_branch_targets_verified'], 7)
        self.assertTrue(info['false_outcome_can_skip_apmmx_callback'])
        self.assertFalse(info['false_outcome_proves_completed_retirement'])
        self.assertFalse(info['permission_to_modify_audio'])
        pos = info['getpos_side_effect']
        self.assertEqual(pos['opcode_windows_verified'], 17)
        self.assertTrue(pos['terminal_reader_status_15_returned_by_getter_as_minus_two'])
        self.assertTrue(pos['terminal_status_is_not_retirement'])
        self.assertEqual(pos['direct_call_edges_verified'], 3)
        self.assertEqual(pos['conditional_branches_verified'], 2)
        self.assertTrue(pos['getpos_is_not_guaranteed_pure'])
        self.assertFalse(pos['getpos_called_during_owners_esc_log_proven'])
        self.assertFalse(pos['completed_driver_retirement_proven'])
        bridge = info['wavmw_getpos_bridge']
        self.assertTrue(bridge['type4_adpcm_dispatch_path_verified'])
        self.assertEqual(bridge['wavmw_opcode_windows_verified'], 12)
        self.assertEqual(bridge['wavmw_direct_edges_verified'], 2)
        self.assertFalse(bridge['getpos_called_during_esc_proven'])
        self.assertFalse(bridge['runtime_audio_change_authorized'])


    def test_unknown_version_refused(self):
        with self.assertRaisesRegex(A.PEFormatError, 'SHA-256 mismatch'):
            A.audit(b'not the original WAVMW', b'not the original APMMX')

    def test_bad_short_jump_rejected(self):
        class BadWav(SyntheticQueryWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x7BA4 + 1] = 0x17
        apm, wav = self.apm, self.wav
        def fake_pe(data):
            return SyntheticQueryApmPE(data) if data == apm else BadWav(data)
        with (patch.object(A, 'PE32', fake_pe),
              patch.object(A, 'audit_wavmw_bridge',
                           lambda w, a: {'static_type4_dispatch_chain_verified': True})):
            with self.assertRaisesRegex(A.PEFormatError, 'gate opcode mismatch|jump target mismatch'):
                A.audit(wav, apm)

    def test_bad_apmmx_branch_rejected(self):
        class BadApm(SyntheticQueryApmPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x1F5F-0x1000] = 0x90
        apm, wav = self.apm, self.wav
        def fake_pe(data):
            return BadApm(data) if data == apm else SyntheticQueryWavPE(data)
        with (patch.object(A, 'PE32', fake_pe),
              patch.object(A, 'audit_wavmw_bridge',
                           lambda w, a: {'static_type4_dispatch_chain_verified': True})):
            with self.assertRaisesRegex(A.PEFormatError, 'gate opcode mismatch'):
                A.audit(wav, apm)

    def test_field34_false_without_native_callback(self):
        x = A.model(field34_nonzero=True, field30_nonzero=True)
        self.assertEqual(x.wavmw_result, 0)
        self.assertFalse(x.apmmx_callback_entered)
        self.assertFalse(x.native_retirement_attempt)
        self.assertTrue(x.wavmw_inner_cleanup and x.wavmw_outer_cleanup)

    def test_field30_zero_true_without_native(self):
        x = A.model(field34_nonzero=False, field30_nonzero=False)
        self.assertEqual(x.wavmw_result, 1)
        self.assertFalse(x.apmmx_callback_entered)
        self.assertFalse(x.wavmw_inner_cleanup)

    def test_missing_delegate_fallback_false(self):
        x = A.model(field34_nonzero=False, field30_nonzero=True,
                    delegate_resolved=False)
        self.assertEqual(x.wavmw_result, 0)
        self.assertFalse(x.apmmx_callback_entered)
        self.assertFalse(x.native_retirement_attempt)

    def test_uninitialized_adpcm_false_no_retirement(self):
        x = A.model(field34_nonzero=False, field30_nonzero=True,
                    delegate_resolved=True, adpcm_initialized=False)
        self.assertTrue(x.apmmx_callback_entered)
        self.assertFalse(x.native_retirement_attempt)

    def test_null_handle_false_no_retirement(self):
        x = A.model(field34_nonzero=False, field30_nonzero=True,
                    delegate_resolved=True, adpcm_initialized=True,
                    handle_present=False)
        self.assertEqual(x.wavmw_result, 0)
        self.assertFalse(x.native_retirement_attempt)

    def test_active_record_true_without_cleanup(self):
        x = A.model(field34_nonzero=False, field30_nonzero=True,
                    delegate_resolved=True, adpcm_initialized=True,
                    handle_present=True, record_active=True)
        self.assertEqual(x.wavmw_result, 1)
        self.assertTrue(x.apmmx_callback_entered)
        self.assertFalse(x.wavmw_inner_cleanup)

    def test_inactive_record_attempt_not_completed_proof(self):
        x = A.model(field34_nonzero=False, field30_nonzero=True,
                    delegate_resolved=True, adpcm_initialized=True,
                    handle_present=True, record_active=False)
        self.assertEqual(x.wavmw_result, 0)
        self.assertTrue(x.native_retirement_attempt)
        self.assertFalse(x.verified_physical_retirement)

    def test_missing_provenance_fails_closed(self):
        for kw in ({}, {'delegate_resolved': True},
                   {'delegate_resolved': True, 'adpcm_initialized': True},
                   {'delegate_resolved': True, 'adpcm_initialized': True,
                    'handle_present': True}):
            with self.subTest(kw=kw):
                with self.assertRaises(ValueError):
                    A.model(field34_nonzero=False, field30_nonzero=True, **kw)

    def test_unknown_flags_fail_closed(self):
        for x, y in ((1, True), (False, 0), (None, True)):
            with self.subTest(x=x, y=y):
                with self.assertRaises(ValueError):
                    A.model(field34_nonzero=x, field30_nonzero=y)

    def test_no_case_proves_physical_retirement(self):
        cases = [
            {'field34_nonzero': True, 'field30_nonzero': True},
            {'field34_nonzero': False, 'field30_nonzero': False},
            {'field34_nonzero': False, 'field30_nonzero': True,
             'delegate_resolved': True, 'adpcm_initialized': True,
             'handle_present': True, 'record_active': False},
        ]
        for case in cases:
            with self.subTest(case=case):
                self.assertFalse(A.model(**case).verified_physical_retirement)


    def test_original_getpos_terminal_status_is_not_retirement(self):
        self.assertEqual(A.classify_original_position_status(15),
                         'completed-sentinel-minus-two-not-retired')
        self.assertEqual(A.classify_original_position_status(0),
                         'position-computed-not-proof-of-lifetime')
        self.assertEqual(A.classify_original_position_status(11),
                         'error-minus-one-not-proof-of-lifetime')
        self.assertEqual(A.classify_original_position_status(1),
                         'error-minus-one-not-proof-of-lifetime')

    def test_position_status_parser_rejects_unknown_bool_or_out_of_range(self):
        for status in (None, True, False, -1, 0x100000000, '15', 15.0):
            with self.subTest(status=status):
                with self.assertRaises(ValueError):
                    A.classify_original_position_status(status)

    def test_getpos_terminal_opcode_must_match_original(self):
        class BadApm(SyntheticQueryApmPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x2A31-0x1000] = 0x90
        with self.assertRaisesRegex(A.PEFormatError, 'getter_status_15_maps_minus_two'):
            A.audit_position_getter(BadApm(b'only synthetic bytes'))

    def test_position_getter_conditional_write_does_not_imply_retirement(self):
        x = A.model_position(active_before=True, gate_dc_nonzero=True,
                             fpu_comparison_skips=False)
        self.assertFalse(x.active_after)
        self.assertTrue(x.active_flag_cleared)
        self.assertFalse(x.native_retirement_proven)

    def test_position_getter_gate_zero_skips_inactivation(self):
        x = A.model_position(active_before=True, gate_dc_nonzero=False,
                             fpu_comparison_skips=False)
        self.assertTrue(x.active_after)
        self.assertFalse(x.active_flag_cleared)

    def test_position_getter_fpu_status_can_skip_inactivation(self):
        x = A.model_position(active_before=True, gate_dc_nonzero=True,
                             fpu_comparison_skips=True)
        self.assertTrue(x.active_after)
        self.assertFalse(x.active_flag_cleared)

    def test_position_getter_already_inactive_is_not_new_retirement(self):
        x = A.model_position(active_before=False, gate_dc_nonzero=True,
                             fpu_comparison_skips=False)
        self.assertFalse(x.active_after)
        self.assertFalse(x.active_flag_cleared)
        self.assertFalse(x.native_retirement_proven)

    def test_position_getter_missing_branch_evidence_fails_closed(self):
        for before, dc, fpu in ((1, True, False), (True, None, False),
                                (True, True, None), (True, True, 0)):
            with self.subTest(values=(before, dc, fpu)):
                with self.assertRaises(ValueError):
                    A.model_position(active_before=before,
                                     gate_dc_nonzero=dc,
                                     fpu_comparison_skips=fpu)

    def test_position_getter_rejects_modified_writer(self):
        class BadApm(SyntheticQueryApmPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x2446-0x1000] = 0x90
        with self.assertRaisesRegex(A.PEFormatError, 'reader_inactive_write'):
            A.audit_position_getter(BadApm(b'synthetic-only'))

    def test_position_getter_rejects_modified_direct_call(self):
        class BadApm(SyntheticQueryApmPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x2A20-0x1000+1] = 0
        with self.assertRaisesRegex(A.PEFormatError, 'getpos_calls_reader|direct-call'):
            A.audit_position_getter(BadApm(b'synthetic-only'))

    def test_position_getter_rejects_modified_branch(self):
        class BadApm(SyntheticQueryApmPE):
            def __init__(self, raw):
                super().__init__(raw)
                # Branch must continue to skip the write when +0xDC=0.
                self.text[0x240B-0x1000+1] = 0x3B
        with self.assertRaisesRegex(A.PEFormatError, 'reader_condition_dc|conditional-branch'):
            A.audit_position_getter(BadApm(b'synthetic-only'))

    def test_position_getter_rejects_unexpected_export(self):
        class BadApm(SyntheticQueryApmPE):
            def exports(self):
                return {'_SND_fn_rGetPosSxdADPCM@4': 0x5200}
        with self.assertRaisesRegex(A.PEFormatError, 'GetPosSxd export mismatch'):
            A.audit_position_getter(BadApm(b'synthetic-only'))

    def test_position_getter_has_no_native_stop_or_play_invocation(self):
        import inspect
        src = inspect.getsource(A.audit_position_getter)
        self.assertNotIn('TestIsPlayingSxd(', src)
        self.assertNotIn('StopTransitionSxd(', src)
        self.assertNotIn('ResumeTransitionSxd(', src)
        self.assertNotIn('WriteProcessMemory(', src)
        self.assertEqual(A.APMMX_POSITION_EDGES['getpos_export_to_core'],
                         (0x51A5, 0x2A00))
        self.assertEqual(A.APMMX_POSITION_EDGES['getpos_core_to_reader'],
                         (0x2A20, 0x2320))



    def test_wavmw_getpos_rejects_changed_dynamic_symbol(self):
        class BadWav(SyntheticQueryWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x21608] = 0
        with self.assertRaisesRegex(A.PEFormatError, 'GetPos ADPCM symbol'):
            A.audit_wavmw_position_bridge(
                BadWav(b'synthetic-only'), SyntheticQueryApmPE(b'synthetic-only'))

    def test_wavmw_getpos_rejects_changed_type4_branch(self):
        class BadWav(SyntheticQueryWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0xBD31] = 0x11
        with self.assertRaisesRegex(A.PEFormatError, 'type4_getpos_branch|branch target'):
            A.audit_wavmw_position_bridge(
                BadWav(b'synthetic-only'), SyntheticQueryApmPE(b'synthetic-only'))

    def test_wavmw_getpos_rejects_rewired_indirect_callback_slot(self):
        class BadWav(SyntheticQueryWavPE):
            def __init__(self, raw):
                super().__init__(raw)
                self.text[0x7CE2+2] = 0x44
        with self.assertRaisesRegex(A.PEFormatError, 'getpos_indirect_callback_slot'):
            A.audit_wavmw_position_bridge(
                BadWav(b'synthetic-only'), SyntheticQueryApmPE(b'synthetic-only'))

    def test_wavmw_getpos_rejects_wrong_export(self):
        class BadWav(SyntheticQueryWavPE):
            def exports(self):
                return {'_SND_fn_rGetPosSxd@4': 0xB000}
        with self.assertRaisesRegex(A.PEFormatError, 'WAVMW GetPosSxd export'):
            A.audit_wavmw_position_bridge(
                BadWav(b'synthetic-only'), SyntheticQueryApmPE(b'synthetic-only'))

    def test_wavmw_getpos_type4_edge_and_dynamic_callback_are_not_observation(self):
        x = A.audit_wavmw_position_bridge(
            SyntheticQueryWavPE(b'synthetic-only'), SyntheticQueryApmPE(b'synthetic-only'))
        self.assertEqual(x['dynamic_function_pointer_rva'], '0x26A14')
        self.assertEqual(x['wavmw_getpos_callback_slot'], '0x40')
        self.assertEqual(A.WAVMW_GETPOS_EDGES['type4_to_position_helper'],
                         (0xBD4F, 0x7CD0))
        self.assertFalse(x['runtime_dynamic_pointer_currently_observed'])
        self.assertFalse(x['getpos_called_during_esc_proven'])



if __name__ == '__main__':
    unittest.main()
