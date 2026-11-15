#!/usr/bin/env python3
"""Reconcile observed *resource provenance* across Rayman 2 Theme switches.

A bounded offline parser for RUNTIME-ESC-THEME-WATCH only. The game emits
original source-verified loaded-bank IDs and original record indices; these
are NOT native driver handles, descriptor-pointer identity, or ADPCM epochs.

No program or DLL is loaded, executed, hooked or modified.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

PREFIX = 'RUNTIME-ESC-THEME-WATCH:'
WORDS = re.compile(r'([a-z][a-z0-9_]*)=([^\s]+)')
REQUIRED = (
    'seq','t_ms','active','ref_class','ref_match','pending_switch_state',
    'pending_target_matches_outer','pending_target_differs_current',
    'outer_candidates','outer_bank','outer_index','alt_bank','alt_index',
    'alt_same_bank','outer_complete','alt_complete',
)

def read_watch_line(text: str) -> dict[str,int] | None:
    if PREFIX not in text:
        return None
    data=dict(WORDS.findall(text.split(PREFIX,1)[1]))
    if any(k not in data for k in REQUIRED):
        return None
    try:
        return {k:int(data[k],10) for k in REQUIRED}
    except ValueError:
        return None


def qualified_current(row: dict[str,int]) -> bool:
    return all((row['active']==1,row['ref_class']==1,row['ref_match']==1,
                row['pending_switch_state']==0,row['outer_complete']==1,
                row['outer_candidates']==1,row['outer_bank']>=0,
                row['outer_index']>=0))


def qualified_pending(row:dict[str,int])->bool:
    return all((row['active']==1,row['ref_class']==3,row['ref_match']==0,
                row['pending_switch_state']==1,row['pending_target_matches_outer']==1,
                row['pending_target_differs_current']==1,
                row['outer_complete']==1,row['alt_complete']==1,
                row['outer_candidates']==1,row['alt_same_bank']==1,
                row['outer_bank']>=0,row['outer_index']>=0,
                row['alt_bank']>=0,row['alt_index']>=0))


def classify(lines:list[str])->dict:
    watches=[x for line in lines if (x:=read_watch_line(line)) is not None]
    warnings=[]
    if any(b['seq']<=a['seq'] or b['t_ms']<a['t_ms']
           for a,b in zip(watches,watches[1:])):
        warnings.append('nonmonotonic watch sequence; candidate rejected')
    matches=[]
    if not warnings:
        for i,row in enumerate(watches):
            if not qualified_pending(row):
                continue
            previous=next((w for w in reversed(watches[:i]) if qualified_current(w)),None)
            following=next((w for w in watches[i+1:] if qualified_current(w)),None)
            old_key=(row['alt_bank'],row['alt_index'])
            new_key=(row['outer_bank'],row['outer_index'])
            prior_same=bool(previous and
                            (previous['outer_bank'],previous['outer_index'])==old_key)
            later_same=bool(following and
                            (following['outer_bank'],following['outer_index'])==new_key)
            # Intermediate snapshots may include ambiguous controller
            # records during a handoff. Do not pretend a contiguous
            # native pointer/driver epoch proof.
            gap_to_prior=None if previous is None else row['seq']-previous['seq']-1
            gap_to_later=None if following is None else following['seq']-row['seq']-1
            matches.append({
                'pending_seq':row['seq'],
                'pending_time_ms':row['t_ms'],
                'current_before_seq':None if previous is None else previous['seq'],
                'current_before_time_ms':None if previous is None else previous['t_ms'],
                'current_after_seq':None if following is None else following['seq'],
                'current_after_time_ms':None if following is None else following['t_ms'],
                'previous_active_outer_resource':None if previous is None else
                    f"{previous['outer_bank']}:{previous['outer_index']}",
                'pending_alternative_resource':f'{old_key[0]}:{old_key[1]}',
                'pending_target_resource':f'{new_key[0]}:{new_key[1]}',
                'next_active_outer_resource':None if following is None else
                    f"{following['outer_bank']}:{following['outer_index']}",
                'last_known_current_matches_pending_alternative':prior_same,
                'first_later_current_matches_pending_target':later_same,
                'snapshots_between_previous_and_pending':gap_to_prior,
                'snapshots_between_pending_and_later':gap_to_later,
                'observed_pending_to_next_current_ms':None if following is None else
                    following['t_ms']-row['t_ms'],
                'observed_pending_to_matched_ms':None if not later_same else
                    following['t_ms']-row['t_ms'],
                'same_descriptor_pointer_proven':False,
                'uninterrupted_pending_or_playback_proven':False,
                'physical_adpcm_generation_continuity_proven':False,
                'permission_to_bypass_early_esc':False,
            })
    return {
        'schema':'rayman2fix-original-theme-handoff-history-v1',
        'watch_records_parsed':len(watches),
        'qualified_pending_observations':len(matches),
        'reconciliations':matches,
        'warnings':warnings,
        'uses_loaded_bank_and_original_resource_index_only':True,
        'native_current_pointer_identity_proven':False,
        'physical_music_voice_generation_proven':False,
        'audio_command_change_authorized':False,
    }


def main(argv=None)->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_log',type=Path)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args(argv)
    try:
        result=classify(args.owner_log.read_text(encoding='utf-8',errors='replace').splitlines())
    except OSError as e:
        parser.exit(2,f'Cannot read owner original log: {e}\n')
    if args.json:
        print(json.dumps(result,sort_keys=True,indent=2))
    else:
        for r in result['reconciliations']:
            print(f"pending {r['pending_time_ms']} ms: "
                  f"last Theme {r['previous_active_outer_resource']} -> "
                  f"alternative {r['pending_alternative_resource']}; "
                  f"pending target {r['pending_target_resource']} -> "
                  f"next Theme {r['next_active_outer_resource']}")
        print('Audio epoch and actual pointer identity: NOT PROVEN')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
