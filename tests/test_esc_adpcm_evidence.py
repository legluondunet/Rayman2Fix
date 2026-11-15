"""Synthetic, non-proprietary classification of native ESC audio logs."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_esc_adpcm_evidence as A


def fixture(stage='pending', cls=3, getpos='resolved', test='resolved'):
    data = [f'RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: ref_class={cls} stage={stage} (read-only)']
    for op in A.OPS:
        status = getpos if op == 'get-pos' else test if op == 'test-is-playing' else 'resolved'
        data.append(f'RUNTIME-AUDIO-ADPCM-DELEGATE: op={op} status={status} '
                    'module=APMMXbvr.dll callback_rva=0x000051A0 (read-only; no call)')
    return data


class EvidenceTests(unittest.TestCase):
    def test_two_complete_stages_compare_equal_without_claiming_invocation(self):
        result=A.classify(fixture()+fixture('synchronized', 1))
        self.assertTrue(result['stage_comparison']['available'])
        self.assertEqual(result['stage_comparison']['changed_operations'], [])
        self.assertFalse(result['can_prove_query_invocation'])
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_dynamic_fallback_is_a_change_not_a_proof_of_call(self):
        result=A.classify(fixture()+fixture('synchronized', 1, getpos='fallback'))
        self.assertEqual(result['stage_comparison']['changed_operations'], ['get-pos'])
        self.assertFalse(result['can_prove_query_invocation'])

    def test_only_legacy_three_callbacks_never_claims_comparison(self):
        older = [f'RUNTIME-AUDIO-ADPCM-DELEGATE: op={op} status=resolved '
                 'module=APMMXbvr.dll callback_rva=0x000054D0'
                 for op in ('stop','pause','resume')]
        result = A.classify(older)
        self.assertEqual(result['delegates']['legacy']['missing'], ['test-is-playing','get-pos'])
        self.assertFalse(result['stage_comparison']['available'])

    def test_bad_stage_class_rejected_and_never_merged(self):
        result=A.classify(fixture('pending', 1))
        self.assertFalse(result['stage_comparison']['available'])
        self.assertTrue(result['warnings'])
        self.assertEqual(len(result['delegates']['legacy']['entries']), 5)

    def test_missing_query_delegate_reports_incomplete(self):
        lines=fixture()[:-1]+fixture('synchronized', 1)
        result=A.classify(lines)
        self.assertEqual(result['delegates']['pending']['missing'], ['get-pos'])
        self.assertFalse(result['stage_comparison']['available'])
        self.assertIsNone(result['stage_comparison']['changed_operations'])

    def test_duplicates_fail_closed_even_with_five_distinct_callbacks(self):
        lines=fixture()+[fixture()[1]]
        result=A.classify(lines)
        self.assertFalse(result['delegates']['pending']['complete'])
        self.assertTrue(result['delegates']['pending']['warnings'])

    def test_open_close_active_snapshot_is_not_epoch(self):
        lines=['RUNTIME-ESC-PREDRIVER-GATE: t_ms=100 transition_pauses=2',
               'RUNTIME-ESC-ADPCM-VOICE: t_ms=100 sampled=1 voice_index=0 paused=1 active=1',
               'RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms=400 paired=1 overlap=0 '
               'play_between=0 stop_between=0',
               'RUNTIME-ESC-ADPCM-RESUME: t_ms=400 paired=1 '
               'sampled=1 voice_index=0 paused=0 active=1']
        result=A.classify(lines)
        self.assertEqual(result['observations']['adpcm_open_samples'],1)
        self.assertEqual(result['observations']['adpcm_close_samples'],1)
        self.assertEqual(result['observations']['export_quiet_lifetimes'],1)
        self.assertFalse(result['can_prove_internal_retirement_absent'])
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_unpaired_close_and_missing_sample_cannot_be_used_as_valid(self):
        result=A.classify(['RUNTIME-ESC-ADPCM-VOICE: sampled=0 active=0',
                           'RUNTIME-ESC-ADPCM-RESUME: paired=0 sampled=1 active=0'])
        self.assertEqual(result['observations']['adpcm_open_samples'],0)
        self.assertEqual(result['observations']['adpcm_close_samples'],0)

    def test_inactive_snapshots_are_reported_without_retirement_claim(self):
        result=A.classify(['RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=0',
                           'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 active=0'])
        self.assertEqual(result['observations']['inactive_open_samples'],1)
        self.assertEqual(result['observations']['inactive_close_samples'],1)
        self.assertFalse(result['can_prove_query_invocation'])

    def test_unknown_op_ignored_with_warning(self):
        result=A.classify(['RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: stage=pending ref_class=3',
                           'RUNTIME-AUDIO-ADPCM-DELEGATE: op=other status=resolved'])
        self.assertFalse(result['delegates']['pending']['complete'])
        self.assertTrue(result['delegates']['pending']['warnings'])

    def test_malformed_ref_value_fails_closed(self):
        result=A.classify(['RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: stage=pending ref_class=nope']
                          +fixture()[1:])
        self.assertFalse(result['stage_comparison']['available'])
        self.assertTrue(result['warnings'])

    def test_repeated_stage_is_ambiguous(self):
        result=A.classify(fixture()+fixture())
        self.assertFalse(result['delegates']['pending']['complete'])
        self.assertFalse(result['stage_comparison']['available'])


if __name__=='__main__':
    unittest.main()
