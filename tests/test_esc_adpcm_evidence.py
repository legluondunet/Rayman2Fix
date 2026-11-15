"""Synthetic, non-proprietary classification of native ESC audio logs."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import audit_esc_adpcm_evidence as A
from audit_adpcm_primary_epoch import SyntheticEpochModel


def fixture(stage='pending', cls=3, getpos='resolved', test='resolved',
            fingerprint=None, module='APMMXbvr.dll', params=False,
            param_status='resolved'):
    data = [f'RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT: ref_class={cls} stage={stage} (read-only)']
    for op in (A.ALL_OPS if params else A.OPS):
        status = (param_status if op in A.PARAM_OPS else
                  getpos if op == 'get-pos' else
                  test if op == 'test-is-playing' else 'resolved')
        suffix = ('' if fingerprint is None else
                  f' original_entry_fingerprint={fingerprint}')
        data.append(f'RUNTIME-AUDIO-ADPCM-DELEGATE: op={op} status={status} '
                    f'module={module} callback_rva={A.ORIGINAL_CALLBACK_RVAS[op]}'
                    f'{suffix} (read-only; no call)')
    return data


class EvidenceTests(unittest.TestCase):
    def test_two_complete_stages_compare_equal_without_claiming_invocation(self):
        result=A.classify(fixture()+fixture('synchronized', 1))
        self.assertTrue(result['stage_comparison']['available'])
        self.assertEqual(result['stage_comparison']['changed_operations'], [])
        self.assertFalse(result['can_prove_query_invocation'])
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_both_stages_fingerprint_original_module_and_expected_five_exports(self):
        result=A.classify(fixture(fingerprint=1)+
                          fixture('synchronized',1,fingerprint=1))
        self.assertTrue(result['stage_comparison']['available'])
        self.assertTrue(result['stage_comparison']['original_entry_fingerprints_both_stages'])
        self.assertEqual(result['delegates']['pending']['original_fingerprinted_operations'],
                         list(A.OPS))
        self.assertFalse(result['stage_comparison']['proves_original_query_was_executed'])
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_legacy_pointers_without_fingerprint_never_qualify(self):
        result=A.classify(fixture()+fixture('synchronized',1))
        self.assertTrue(result['stage_comparison']['available'])
        self.assertFalse(result['stage_comparison']['original_entry_fingerprints_both_stages'])
        self.assertEqual(result['delegates']['pending']['original_fingerprinted_operations'],[])

    def test_wrong_rva_does_not_qualify_even_when_flag_is_one(self):
        lines=fixture(fingerprint=1)+fixture('synchronized',1,fingerprint=1)
        for i,row in enumerate(lines):
            if 'op=get-pos ' in row and 'original_entry_fingerprint=1' in row:
                lines[i]=row.replace('callback_rva=0x000051A0',
                                     'callback_rva=0x000051B0')
                break
        result=A.classify(lines)
        self.assertFalse(result['stage_comparison']['original_entry_fingerprints_both_stages'])
        self.assertNotIn('get-pos',
                         result['delegates']['pending']['original_fingerprinted_operations'])

    def test_wrong_module_name_or_false_flag_cannot_qualify_original(self):
        for lines in (
            fixture(fingerprint=1,module='OtherAudio.dll')+
                fixture('synchronized',1,fingerprint=1),
            fixture(fingerprint=0)+fixture('synchronized',1,fingerprint=1),
            fixture(fingerprint='unknown')+
                fixture('synchronized',1,fingerprint=1),
        ):
            with self.subTest(input=lines[1]):
                report=A.classify(lines)
                self.assertFalse(
                    report['stage_comparison']['original_entry_fingerprints_both_stages'])
                self.assertFalse(report['stage_comparison']['proves_original_query_was_executed'])

    def test_matching_five_verified_pointers_and_endpoints_cannot_exclude_aba(self):
        # Two physically different native histories are indistinguishable
        # to the limited current diagnostic. Do not infer a musical epoch
        # from even five exact original callbacks and two active samples.
        log=fixture(fingerprint=1)+[
            'RUNTIME-ESC-PREDRIVER-GATE: t_ms=100 transition_pauses=2',
            'RUNTIME-ESC-ADPCM-VOICE: t_ms=100 sampled=1 voice_index=0 '
            'paused=1 active=1',
            'RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms=400 paired=1 overlap=0 '
            'play_between=0 stop_between=0',
            'RUNTIME-ESC-ADPCM-RESUME: t_ms=400 paired=1 sampled=1 '
            'voice_index=0 paused=0 active=1',
        ]+fixture('synchronized',1,fingerprint=1)
        continuous=SyntheticEpochModel()
        first=continuous.primary_commit(7)
        self.assertTrue(continuous.matches_conceptual_epoch(7,first))
        recycled=SyntheticEpochModel()
        prior=recycled.primary_commit(7)
        # No observed exported Stop; original primary can reuse a
        # physical record slot, retaining the native handle and count.
        current=recycled.primary_commit(7)
        self.assertNotEqual(prior,current)
        self.assertFalse(recycled.matches_conceptual_epoch(7,prior))
        # Both histories produce these IDENTICAL five-delegate and
        # active=1 endpoint records; only the hidden native epoch differs.
        report=A.classify(log)
        self.assertTrue(
            report['stage_comparison']['original_entry_fingerprints_both_stages'])
        self.assertEqual(report['observations']['adpcm_open_samples'],1)
        self.assertEqual(report['observations']['adpcm_close_samples'],1)
        self.assertEqual(report['observations']['export_quiet_lifetimes'],1)
        self.assertFalse(report['can_prove_internal_retirement_absent'])
        self.assertFalse(report['can_prove_continuous_physical_voice_generation'])

    def test_both_stages_fingerprint_all_seven_original_delegates(self):
        report=A.classify(
            fixture(fingerprint=1,params=True)+
            fixture('synchronized',1,fingerprint=1,params=True))
        self.assertTrue(report['stage_comparison']['available'])
        self.assertTrue(report['stage_comparison']['parameter_comparison_available'])
        self.assertEqual(report['stage_comparison']['changed_parameter_operations'],[])
        self.assertTrue(report['stage_comparison'][
            'all_seven_original_entry_fingerprints_both_stages'])
        self.assertEqual(len(report['delegates']['pending']['entries']),7)
        self.assertFalse(report['can_prove_query_invocation'])
        self.assertFalse(report['can_prove_internal_retirement_absent'])

    def test_legacy_five_pointer_stage_is_not_full_seven(self):
        report=A.classify(fixture(fingerprint=1)+
                          fixture('synchronized',1,fingerprint=1))
        self.assertTrue(report['stage_comparison']['available'])
        self.assertTrue(report['stage_comparison']['original_entry_fingerprints_both_stages'])
        self.assertFalse(report['stage_comparison']['parameter_comparison_available'])
        self.assertIsNone(report['stage_comparison']['changed_parameter_operations'])
        self.assertFalse(report['stage_comparison'][
            'all_seven_original_entry_fingerprints_both_stages'])
        self.assertEqual(report['delegates']['pending']['missing_seven'],
                         list(A.PARAM_OPS))

    def test_changed_setparam_pointer_does_not_prove_that_it_was_invoked(self):
        report=A.classify(
            fixture(fingerprint=1,params=True)+
            fixture('synchronized',1,fingerprint=1,params=True,
                    param_status='fallback'))
        self.assertTrue(report['stage_comparison']['available'])
        self.assertEqual(report['stage_comparison']['changed_operations'],[])
        self.assertTrue(report['stage_comparison']['parameter_comparison_available'])
        self.assertEqual(report['stage_comparison']['changed_parameter_operations'],
                         list(A.PARAM_OPS))
        self.assertFalse(report['stage_comparison'][
            'all_seven_original_entry_fingerprints_both_stages'])
        self.assertFalse(report['stage_comparison']['proves_original_query_was_executed'])

    def test_one_missing_setparam_prevents_complete_parameter_comparison(self):
        first=fixture(params=True,fingerprint=1)
        first=[line for line in first if 'op=set-param-transition ' not in line]
        report=A.classify(first+fixture('synchronized',1,params=True,fingerprint=1))
        self.assertTrue(report['stage_comparison']['available'])
        self.assertFalse(report['stage_comparison']['parameter_comparison_available'])
        self.assertIsNone(report['stage_comparison']['changed_parameter_operations'])
        self.assertEqual(report['delegates']['pending']['missing_seven'],
                         ['set-param-transition'])
        self.assertFalse(report['can_prove_continuous_physical_voice_generation'])

    def test_duplicate_parameter_pointer_fails_closed(self):
        first=fixture(params=True,fingerprint=1)
        report=A.classify(first+[first[-1]]+
                          fixture('synchronized',1,params=True,fingerprint=1))
        self.assertFalse(report['delegates']['pending']['complete_seven'])
        self.assertFalse(report['stage_comparison']['parameter_comparison_available'])
        self.assertFalse(report['stage_comparison'][
            'all_seven_original_entry_fingerprints_both_stages'])

    def test_dynamic_fallback_is_a_change_not_a_proof_of_call(self):
        result=A.classify(fixture()+fixture('synchronized', 1, getpos='fallback'))
        self.assertEqual(result['stage_comparison']['changed_operations'], ['get-pos'])
        self.assertFalse(result['can_prove_query_invocation'])

    def test_buffer_output_positions_pair_but_never_prove_music_generation(self):
        log=[
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=1 '
            'mode0_output=1024 mode1_output=2048',
            'RUNTIME-ESC-PREDRIVER-LIFETIME: paired=1 overlap=0 '
            'play_between=0 stop_between=0',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 active=1 '
            'mode0_output=1536 mode1_output=3072',
        ]
        info=A.classify(log)
        output=info['native_output_positions']
        self.assertEqual(output['paired_counter_snapshots'],1)
        self.assertEqual(output['mode0_increased_pairs'],1)
        self.assertEqual(output['mode1_increased_pairs'],1)
        self.assertFalse(output['proves_original_mixer_callback_was_invoked'])
        self.assertFalse(output['proves_music_voice_progressed'])
        self.assertFalse(info['can_prove_continuous_physical_voice_generation'])

    def test_buffer_output_positions_unchanged_are_not_stable_voice_proof(self):
        info=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 mode0_output=1024 mode1_output=2048',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 '
            'mode0_output=1024 mode1_output=2048',
        ])
        self.assertEqual(info['native_output_positions']['paired_counter_snapshots'],1)
        self.assertEqual(info['native_output_positions']['mode0_increased_pairs'],0)
        self.assertFalse(info['native_output_positions']['proves_music_voice_progressed'])

    def test_buffer_output_counter_drop_does_not_mean_negative_or_stable_progress(self):
        info=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 mode0_output=4294967290 mode1_output=6000',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 '
            'mode0_output=120 mode1_output=7000',
        ])
        output=info['native_output_positions']
        self.assertEqual(output['paired_counter_snapshots'],1)
        self.assertEqual(output['counter_decreased_or_wrapped_pairs'],1)
        self.assertEqual(output['mode1_increased_pairs'],0)

    def test_overlapping_esc_opens_do_not_mix_output_counter_snapshots(self):
        info=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 mode0_output=10 mode1_output=10',
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 mode0_output=20 mode1_output=20',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 '
            'mode0_output=100 mode1_output=100',
        ])
        self.assertEqual(info['native_output_positions']['paired_counter_snapshots'],0)
        self.assertEqual(info['native_output_positions']['overlapping_open_pairs_refused'],1)

    def test_missing_or_malformed_output_counters_fail_closed(self):
        bad=[
            ('mode0_output=-1 mode1_output=1', 'mode0_output=2 mode1_output=3'),
            ('mode0_output=4294967296 mode1_output=1', 'mode0_output=2 mode1_output=3'),
            ('mode0_output=abc mode1_output=1', 'mode0_output=2 mode1_output=3'),
            ('mode0_output=1', 'mode0_output=2 mode1_output=3'),
        ]
        for op,cl in bad:
            with self.subTest(op=op):
                info=A.classify([
                    'RUNTIME-ESC-ADPCM-VOICE: sampled=1 '+op,
                    'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=1 '+cl,
                ])
                self.assertEqual(
                    info['native_output_positions']['paired_counter_snapshots'],0)

    def test_unpaired_closing_output_counters_are_not_compared(self):
        info=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 mode0_output=1 mode1_output=1',
            'RUNTIME-ESC-ADPCM-RESUME: paired=0 sampled=1 '
            'mode0_output=100 mode1_output=100',
        ])
        self.assertEqual(info['native_output_positions']['paired_counter_snapshots'],0)
        self.assertFalse(info['native_output_positions']['proves_original_mixer_callback_was_invoked'])

    def test_closing_gate_passed_with_live_sample_is_counted(self):
        result=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=1 '
            'mode0_output=100 mode1_output=100',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 gate=1 sampled=1 '
            'active=1 mode0_output=200 mode1_output=200',
        ])
        obs=result['observations']
        self.assertEqual(obs['close_gates_open'],1)
        self.assertEqual(obs['close_gates_blocked'],0)
        self.assertEqual(obs['close_gate_passed_but_sample_unknown'],0)
        self.assertEqual(obs['adpcm_close_samples'],1)
        self.assertEqual(result['native_output_positions']['paired_counter_snapshots'],1)
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_closing_gate_blocked_is_not_a_missing_audio_voice(self):
        result=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=1 '
            'mode0_output=100 mode1_output=100',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 gate=0 sampled=0 '
            'active=-1 mode0_output=0 mode1_output=0',
        ])
        obs=result['observations']
        self.assertEqual(obs['close_gates_open'],0)
        self.assertEqual(obs['close_gates_blocked'],1)
        self.assertEqual(obs['close_gate_passed_but_sample_unknown'],0)
        self.assertEqual(obs['adpcm_close_samples'],0)
        self.assertEqual(result['native_output_positions']['paired_counter_snapshots'],0)
        self.assertFalse(result['can_prove_internal_retirement_absent'])

    def test_closing_gate_passed_but_unreadable_voice_is_unknown(self):
        result=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=1',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 gate=1 sampled=0 '
            'voice_index=-1 active=-1',
        ])
        obs=result['observations']
        self.assertEqual(obs['close_gates_open'],1)
        self.assertEqual(obs['close_gate_passed_but_sample_unknown'],1)
        self.assertEqual(obs['adpcm_close_samples'],0)
        self.assertFalse(result['can_prove_continuous_physical_voice_generation'])

    def test_legacy_close_without_gate_field_is_unknown_not_blocked(self):
        result=A.classify([
            'RUNTIME-ESC-ADPCM-VOICE: sampled=1 active=1',
            'RUNTIME-ESC-ADPCM-RESUME: paired=1 sampled=0 active=-1',
        ])
        obs=result['observations']
        self.assertEqual(obs['close_gates_open'],0)
        self.assertEqual(obs['close_gates_blocked'],0)
        self.assertEqual(obs['close_gate_passed_but_sample_unknown'],0)
        self.assertEqual(obs['adpcm_close_samples'],0)

    def test_owner_shaped_three_live_buffer_pairs_and_two_stream_switch_refusals(self):
        # Synthetic/abstracted reconstruction of the 15:34 behaviour.
        # No proprietary game bytes, actual owner log, or native calls.
        # Three ref_class=3 source-paired menus, each with active=1
        # and paused=1 after native Pause, have nonzero global output
        # progress and valid post-native Resume snapshots.
        # Two later ref_class=1 sessions change SXD while menu is open:
        # observer correctly refuses to sample those new streams.
        log=[]
        for start, end, old_pos, new_pos in (
            (1000, 7333, 100000, 239920),
            (10000, 18217, 300000, 481500),
            (22000, 31333, 500000, 706140),
        ):
            log.extend([
                f'RUNTIME-ESC-PREDRIVER-GATE: t_ms={start} '
                'callbacks=8 transition_pauses=2 native_ref_class=3 native_ref_match=0',
                f'RUNTIME-ESC-ADPCM-VOICE: t_ms={start} '
                f'sampled=1 voice_index=0 paused=1 active=1 '
                f'mode0_output={old_pos} mode1_output={old_pos}',
                f'RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms={end} paired=1 overlap=0 '
                'play_between=0 stop_between=0',
                f'RUNTIME-ESC-ADPCM-RESUME: t_ms={end} paired=1 gate=1 sampled=1 '
                f'voice_index=0 paused=0 active=1 '
                f'mode0_output={new_pos} mode1_output={new_pos}',
            ])
        for start,end in ((34000,40633),(42000,45100)):
            log.extend([
                f'RUNTIME-ESC-PREDRIVER-GATE: t_ms={start} '
                'callbacks=10 transition_pauses=2 native_ref_class=1 native_ref_match=1',
                f'RUNTIME-ESC-ADPCM-VOICE: t_ms={start} sampled=1 '
                'voice_index=0 paused=0 active=1 '
                'mode0_output=900000 mode1_output=900000',
                f'RUNTIME-ESC-PREDRIVER-LIFETIME: t_ms={end} paired=1 overlap=0 '
                'play_between=0 stop_between=0',
                f'RUNTIME-ESC-ADPCM-RESUME: t_ms={end} paired=1 '
                'gate=0 sampled=0 active=-1 mode0_output=0 mode1_output=0',
            ])
        # Last opening intentionally has no paired native Resume.
        log.extend([
            'RUNTIME-ESC-PREDRIVER-GATE: t_ms=50000 callbacks=8 '
            'transition_pauses=2 native_ref_class=1 native_ref_match=1',
            'RUNTIME-ESC-ADPCM-VOICE: t_ms=50000 sampled=1 '
            'voice_index=0 paused=0 active=1 '
            'mode0_output=1100000 mode1_output=1100000',
        ])
        report=A.classify(log)
        self.assertEqual(report['observations']['esc_gate_count'],6)
        self.assertEqual(report['observations']['adpcm_open_samples'],6)
        self.assertEqual(report['observations']['paired_menu_lifetimes'],5)
        self.assertEqual(report['observations']['export_quiet_lifetimes'],5)
        self.assertEqual(report['observations']['adpcm_close_samples'],3)
        self.assertEqual(report['observations']['close_gates_open'],3)
        self.assertEqual(report['observations']['close_gates_blocked'],2)
        self.assertEqual(report['observations']['close_gate_passed_but_sample_unknown'],0)
        output=report['native_output_positions']
        self.assertEqual(output['paired_counter_snapshots'],3)
        self.assertEqual(output['mode0_increased_pairs'],3)
        self.assertEqual(output['mode1_increased_pairs'],3)
        self.assertEqual(output['counter_decreased_or_wrapped_pairs'],0)
        self.assertFalse(output['proves_music_voice_progressed'])
        self.assertFalse(output['proves_original_mixer_callback_was_invoked'])
        self.assertFalse(report['can_prove_internal_retirement_absent'])
        self.assertFalse(report['can_prove_continuous_physical_voice_generation'])

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
