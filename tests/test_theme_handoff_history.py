"""Synthetic predecessor/pending/synchronized Theme controller evidence tests."""
from __future__ import annotations
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit_theme_handoff_history as A


def watch(seq,t,cls,match,flag,outer,alt=(-1,-1),target=(-1,-1),
          active=1,complete=1,candidates=1):
    return ('RUNTIME-ESC-THEME-WATCH: '
            f'seq={seq} t_ms={t} active={active} ref_class={cls} '
            f'ref_match={match} pending_switch_state={flag} '
            f'pending_target_matches_outer={target[0]} '
            f'pending_target_differs_current={target[1]} '
            f'outer_candidates={candidates} outer_bank={outer[0]} '
            f'outer_index={outer[1]} alt_bank={alt[0]} alt_index={alt[1]} '
            f'alt_same_bank={int(alt[0]==outer[0]) if alt[0]>=0 else -1} '
            f'outer_complete={complete} alt_complete={int(alt[0]>=0)}')


class ThemeHistoryTests(unittest.TestCase):
    def owner_shaped(self):
        return [
            watch(14,34849,1,1,0,(9,12)),
            watch(15,35700,-1,-1,-1,(-1,-1),active=1,
                  complete=0,candidates=2),
            watch(16,35749,3,0,1,(9,5),alt=(9,12),target=(1,1)),
            watch(17,63549,1,1,0,(9,5)),
        ]

    def test_previous_theme_resource_explains_pending_alternative(self):
        result=A.classify(self.owner_shaped())
        self.assertEqual(result['watch_records_parsed'],4)
        self.assertEqual(result['qualified_pending_observations'],1)
        match=result['reconciliations'][0]
        self.assertEqual(match['previous_active_outer_resource'],'9:12')
        self.assertEqual(match['pending_alternative_resource'],'9:12')
        self.assertEqual(match['pending_target_resource'],'9:5')
        self.assertEqual(match['next_active_outer_resource'],'9:5')
        self.assertTrue(match['last_known_current_matches_pending_alternative'])
        self.assertTrue(match['first_later_current_matches_pending_target'])
        self.assertEqual(match['observed_pending_to_matched_ms'],27800)
        self.assertEqual(match['snapshots_between_previous_and_pending'],1)
        self.assertEqual(match['snapshots_between_pending_and_later'],0)
        self.assertFalse(match['same_descriptor_pointer_proven'])
        self.assertFalse(match['physical_adpcm_generation_continuity_proven'])
        self.assertFalse(match['permission_to_bypass_early_esc'])

    def test_mismatching_alternative_is_not_previous_theme(self):
        rows=self.owner_shaped()
        rows[2]=watch(16,35749,3,0,1,(9,5),alt=(9,11),target=(1,1))
        result=A.classify(rows)['reconciliations'][0]
        self.assertFalse(result['last_known_current_matches_pending_alternative'])
        self.assertTrue(result['first_later_current_matches_pending_target'])

    def test_mismatching_new_current_cannot_be_claimed_committed(self):
        rows=self.owner_shaped()
        rows[3]=watch(17,63549,1,1,0,(9,6))
        result=A.classify(rows)['reconciliations'][0]
        self.assertFalse(result['first_later_current_matches_pending_target'])
        self.assertEqual(result['next_active_outer_resource'],'9:6')

    def test_unverified_pending_target_never_qualifies(self):
        rows=self.owner_shaped()
        rows[2]=watch(16,35749,3,0,1,(9,5),alt=(9,12),target=(0,1))
        self.assertEqual(A.classify(rows)['qualified_pending_observations'],0)

    def test_missing_alt_bank_provenance_refuses_qualification(self):
        rows=self.owner_shaped()
        rows[2]=watch(16,35749,3,0,1,(9,5),alt=(-1,-1),target=(1,1))
        self.assertEqual(A.classify(rows)['qualified_pending_observations'],0)

    def test_malformed_watch_line_is_skipped(self):
        bad=watch(16,35749,3,0,1,(9,5),alt=(9,12),target=(1,1))
        bad=bad.replace('t_ms=35749','t_ms=not-a-number')
        result=A.classify([self.owner_shaped()[0],bad,self.owner_shaped()[-1]])
        self.assertEqual(result['watch_records_parsed'],2)
        self.assertEqual(result['qualified_pending_observations'],0)

    def test_nonmonotonic_or_duplicated_sequence_is_fail_closed(self):
        for rows in (
            [self.owner_shaped()[0],self.owner_shaped()[0],self.owner_shaped()[2]],
            [self.owner_shaped()[0],watch(16,100,3,0,1,(9,5),
                                           alt=(9,12),target=(1,1))],
        ):
            with self.subTest(rows=rows):
                result=A.classify(rows)
                self.assertTrue(result['warnings'])
                self.assertEqual(result['qualified_pending_observations'],0)

    def test_pending_observation_without_known_previous_remains_incomplete(self):
        result=A.classify(self.owner_shaped()[2:])
        m=result['reconciliations'][0]
        self.assertIsNone(m['current_before_seq'])
        self.assertFalse(m['last_known_current_matches_pending_alternative'])

    def test_pending_observation_without_future_sync_is_incomplete(self):
        result=A.classify(self.owner_shaped()[:3])
        m=result['reconciliations'][0]
        self.assertIsNone(m['observed_pending_to_matched_ms'])
        self.assertFalse(m['first_later_current_matches_pending_target'])

    def test_output_counters_and_music_pause_never_authorize_control(self):
        lines=self.owner_shaped()
        lines.insert(3,'RUNTIME-ESC-ADPCM-VOICE: sampled=1 paused=1 active=1 '
                     'mode0_output=123 mode1_output=123')
        r=A.classify(lines)
        self.assertFalse(r['audio_command_change_authorized'])
        self.assertFalse(r['physical_music_voice_generation_proven'])
        self.assertFalse(r['native_current_pointer_identity_proven'])


if __name__=='__main__':
    unittest.main()
