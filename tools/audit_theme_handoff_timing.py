#!/usr/bin/env python3
"""Read-only, fail-closed comparison of original Theme pending handoffs.

Uses ONLY recorded original Theme watcher events and source-paired
script ESC intervals. Elapsed time minus observed ESC time is NOT a
native engine timer measurement, nor proof of an ADPCM voice generation.
No game, native driver or audio query is executed.
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

KV = re.compile(r'([a-z][a-z0-9_]*)=([^\s]+)')
WATCH = 'RUNTIME-ESC-THEME-WATCH:'
OPEN = 'RUNTIME-ESC-PREDRIVER-GATE:'
CLOSE = 'RUNTIME-ESC-PREDRIVER-LIFETIME:'


def kv(line: str) -> dict[str, str]:
    return dict(KV.findall(line))


def int_field(row: dict, key: str) -> int | None:
    try:
        value = row[key]
        if not re.fullmatch(r'-?\d+', value):
            return None
        return int(value, 10)
    except (ValueError, KeyError):
        return None


def native_key(row: dict, prefix: str) -> tuple[int, int] | None:
    bank, index = int_field(row, prefix+'bank'), int_field(row, prefix+'index')
    if bank is None or index is None or bank < 0 or index < 0:
        return None
    return bank, index


def watched_current(row: dict) -> bool:
    return (int_field(row, 'active') == 1 and
            int_field(row, 'controller_field0c') == 0 and
            int_field(row, 'ref_class') == 1 and
            int_field(row, 'ref_match') == 1 and
            int_field(row, 'pending_switch_state') == 0 and
            int_field(row, 'outer_candidates') == 1 and
            int_field(row, 'outer_complete') == 1 and
            native_key(row, 'outer_') is not None)


def watched_pending(row: dict) -> bool:
    return (int_field(row, 'active') == 1 and
            int_field(row, 'controller_field0c') == 1 and
            int_field(row, 'ref_class') == 3 and
            int_field(row, 'ref_match') == 0 and
            int_field(row, 'pending_switch_state') == 1 and
            int_field(row, 'pending_target_matches_outer') == 1 and
            int_field(row, 'pending_target_differs_current') == 1 and
            int_field(row, 'outer_candidates') == 1 and
            int_field(row, 'outer_complete') == 1 and
            int_field(row, 'alt_complete') == 1 and
            int_field(row, 'alt_same_bank') == 1 and
            native_key(row, 'outer_') is not None and
            native_key(row, 'alt_') is not None)


def scan(lines: list[str]) -> dict:
    rows = {WATCH: [], OPEN: [], CLOSE: []}
    for index, line in enumerate(lines):
        for prefix in rows:
            if prefix in line:
                data = kv(line.split(prefix, 1)[1])
                t = int_field(data, 't_ms')
                if t is not None and t >= 0:
                    rows[prefix].append((t, index, data))
                break
    for prefix in rows:
        rows[prefix].sort(key=lambda x: (x[0], x[1]))
    watch, gates, closings = rows[WATCH], rows[OPEN], rows[CLOSE]
    results = []
    for wi, (start, _, state) in enumerate(watch):
        if not watched_pending(state):
            continue
        current = native_key(state, 'outer_')
        former = native_key(state, 'alt_')
        previous = next((w for w in reversed(watch[:wi])
                         if watched_current(w[2])), None)
        prev_matches = bool(previous and native_key(previous[2], 'outer_') == former)
        # Look at the last *change-triggered* original inner SXD part
        # while the former Theme was current. A 4.8-second cadence is
        # evidence of part progression, not a proved callback schedule.
        previous_inner = native_key(previous[2], 'inner_') if previous else None
        prior_part = next((
            w for w in reversed(watch[:wi]) if previous is not None and
            w[0] < previous[0] and watched_current(w[2]) and
            native_key(w[2], 'outer_') == former and
            native_key(w[2], 'inner_') is not None and
            previous_inner is not None and
            native_key(w[2], 'inner_')[0] == previous_inner[0] and
            native_key(w[2], 'inner_')[1] != previous_inner[1]), None)
        commit = next((w for w in watch[wi+1:]
                       if w[0] > start and watched_current(w[2]) and
                       native_key(w[2], 'outer_') == current), None)
        conflicting = any(
            w[0] > start and (commit is None or w[0] < commit[0]) and
            (watched_pending(w[2]) and
             (native_key(w[2], 'outer_') != current or
              native_key(w[2], 'alt_') != former) or
             watched_current(w[2]) and
             native_key(w[2], 'outer_') != current)
            for w in watch[wi+1:])
        paired = []
        invalid = []
        if commit is not None:
            opens = [g for g in gates if start < g[0] < commit[0]]
            region_closes = [c for c in closings if start < c[0] < commit[0]]
            used = set()
            for gi, g in enumerate(opens):
                until = opens[gi+1][0] if gi+1 < len(opens) else commit[0]
                candidates = [ci for ci, c in enumerate(region_closes)
                              if ci not in used and g[0] < c[0] < until]
                if (len(candidates) != 1 or
                    int_field(g[2], 'native_ref_class') != 3 or
                    int_field(g[2], 'native_ref_match') != 0 or
                    native_key(g[2], 'outer_') != current):
                    invalid.append('unmatched or non-pending source ESC open')
                    continue
                cidx = candidates[0]
                c = region_closes[cidx]
                elapsed = c[0] - g[0]
                if (int_field(c[2], 'paired') != 1 or
                    int_field(c[2], 'overlap') != 0 or
                    int_field(c[2], 'elapsed_ms') != elapsed or
                    elapsed <= 0):
                    invalid.append('unverified original ESC close or duration')
                    continue
                used.add(cidx)
                paired.append({'open_ms': g[0], 'close_ms': c[0],
                               'elapsed_ms': elapsed})
            if len(used) != len(region_closes):
                invalid.append('orphan source ESC closing')
            if not opens:
                invalid.append('no witnessed first-ESC interval')
        evidence_complete = bool(previous and prev_matches and commit and
                                 not conflicting and not invalid and paired)
        span = commit[0]-start if evidence_complete else None
        paused = sum(x['elapsed_ms'] for x in paired) if evidence_complete else None
        remainder = span-paused if span is not None else None
        if remainder is not None and remainder < 0:
            invalid.append('observed menu intervals exceed pending window')
            evidence_complete = False
            span = paused = remainder = None
        results.append({
            'pending_first_verified_ms': start,
            'previous_current_last_sample_ms': previous[0] if previous else None,
            'previous_current_matches_alternative': prev_matches,
            'previous_inner_part_bank_index': (
                list(previous_inner) if previous_inner else None),
            'prior_inner_part_bank_index': (
                list(native_key(prior_part[2], 'inner_')) if prior_part else None),
            'prior_part_change_sample_ms': prior_part[0] if prior_part else None,
            'previous_inner_part_sample_ms': previous[0] if previous else None,
            'prior_observed_part_interval_ms': (
                previous[0]-prior_part[0] if previous and prior_part else None),
            'pending_requested_after_last_part_sample_ms': (
                start-previous[0] if previous and start>previous[0] else None),
            'previous_part_to_commit_minus_esc_ms': (
                commit[0]-previous[0]-paused
                if evidence_complete and prior_part and previous and
                   previous_inner is not None else None),
            'part_boundary_callback_execution_proven': False,
            'former_theme_bank_index': list(former) if former else None,
            'pending_target_bank_index': list(current) if current else None,
            'new_current_first_verified_ms': commit[0] if commit else None,
            'source_esc_intervals': paired if evidence_complete else [],
            'complete_source_evidence': evidence_complete,
            'issues': invalid + (['different previous Theme'] if not prev_matches else []) +
                      (['intervening conflicting Theme state'] if conflicting else []) +
                      (['no verified commit'] if commit is None else []),
            'sampled_pending_wall_ms': span,
            'source_paired_esc_ms': paused,
            'wall_minus_source_esc_ms': remainder,
            'pause_suspends_theme_switch_proven': False,
            'native_music_generation_proven': False,
        })
    return {'schema': 'rayman2fix-original-theme-handoff-v1',
            'watch_count': len(watch),
            'source_pause_count': len(gates),
            'source_resume_count': len(closings),
            'handoffs': results,
            'interpretation': ('Wall time minus source-paired ESC intervals is only an '
                               'observed residual between two controller state samples. '
                               'Correlation does not prove the original game timer, '
                               'driver lifetime or per-voice playback.')}


def compare(reports: list[dict]) -> dict:
    valid = [h for r in reports for h in r['handoffs']
             if h['complete_source_evidence']]
    if len(valid) < 2:
        return {'comparison_available': False}
    residuals = [h['wall_minus_source_esc_ms'] for h in valid]
    wall = [h['sampled_pending_wall_ms'] for h in valid]
    menu = [h['source_paired_esc_ms'] for h in valid]
    return {'comparison_available': True, 'evidence_windows': len(valid),
            'wall_spread_ms': max(wall)-min(wall),
            'menu_spread_ms': max(menu)-min(menu),
            'outside_esc_residual_spread_ms': max(residuals)-min(residuals),
            'same_underlying_game_timer_proven': False}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('logs', type=Path, nargs='+')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args(argv)
    try:
        reports=[scan(p.read_text(encoding='utf-8',
                                  errors='replace').splitlines())
                 for p in args.logs]
    except OSError as exc:
        print(f'Could not read owner log: {exc}',file=sys.stderr)
        return 2
    result={'logs': [{'filename': p.name, **r}
                     for p,r in zip(args.logs,reports)],
            'comparison': compare(reports)}
    if args.json:
        print(json.dumps(result,indent=2,sort_keys=True))
    else:
        for log in result['logs']:
            print(log['filename'])
            for h in log['handoffs']:
                print(' ',h['former_theme_bank_index'],'->',
                      h['pending_target_bank_index'],
                      'wall_ms=',h['sampled_pending_wall_ms'],
                      'esc_ms=',h['source_paired_esc_ms'],
                      'outside_esc_ms=',h['wall_minus_source_esc_ms'],
                      'complete=',h['complete_source_evidence'])
        print('Cross-log comparison:',result['comparison'])
    return 0


if __name__=='__main__':
    raise SystemExit(main())
