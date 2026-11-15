#!/usr/bin/env python3
"""Conservative, read-only ESC ADPCM log evidence classifier.

Only classifies observed log fields; never treats a resolved callback as
an invocation, a zero exported Stop count as absence of retirement, or
identical sampled slot/handle as a native physical voice generation.
No proprietary game bytes and no process or DLL calls.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

OPS = ('stop', 'pause', 'resume', 'test-is-playing', 'get-pos')
PARAM_OPS = ('set-param', 'set-param-transition')
ALL_OPS = OPS + PARAM_OPS
# These are independently verified exported RVAs in the owner's exact
# 1999 APMMX DLL, not physical voice identities or evidence of a call.
ORIGINAL_CALLBACK_RVAS = {
    'stop': '0x000054C0',
    'pause': '0x000054D0',
    'resume': '0x000054E0',
    'test-is-playing': '0x00005150',
    'get-pos': '0x000051A0',
    'set-param': '0x00005120',
    'set-param-transition': '0x00005430',
}

STAGES = {'pending': 3, 'synchronized': 1}
PREFIX_CONTEXT = 'RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT:'
PREFIX_DELEGATE = 'RUNTIME-AUDIO-ADPCM-DELEGATE:'
PREFIX_OPEN = 'RUNTIME-ESC-ADPCM-VOICE:'
PREFIX_CLOSE = 'RUNTIME-ESC-ADPCM-RESUME:'
PREFIX_PAIR = 'RUNTIME-ESC-PREDRIVER-LIFETIME:'
PREFIX_GATE = 'RUNTIME-ESC-PREDRIVER-GATE:'
KEY_VAL = re.compile(r'([a-z][a-z0-9_]*)=([^\s]+)')


def fields(s: str) -> dict[str, str]:
    return dict(KEY_VAL.findall(s))


def classify(lines: list[str]) -> dict:
    groups: dict[str, dict] = {k: {'entries': {}, 'warnings': []} for k in (*STAGES, 'legacy')}
    current: str | None = None
    current_count = 0
    observations = {'esc_gate_count': 0, 'adpcm_open_samples': 0,
                    'adpcm_close_samples': 0, 'paired_menu_lifetimes': 0,
                    'export_quiet_lifetimes': 0, 'inactive_open_samples': 0,
                    'inactive_close_samples': 0}
    warnings: list[str] = []
    for raw in lines:
        line = raw.strip()
        if PREFIX_CONTEXT in line:
            info = fields(line.split(PREFIX_CONTEXT, 1)[1])
            name = info.get('stage', '')
            try:
                ref = int(info.get('ref_class', '-1'))
            except ValueError:
                ref = -1
            if name not in STAGES or STAGES[name] != ref:
                current = None
                warnings.append('invalid delegate context; subsequent pointers not trusted')
            else:
                current = name
                if groups[name]['entries']:
                    groups[name]['warnings'].append('repeated context; duplicate entries are ambiguous')
                current_count = 0
            continue
        if PREFIX_DELEGATE in line:
            info = fields(line.split(PREFIX_DELEGATE, 1)[1])
            group = current if current is not None else 'legacy'
            op = info.get('op')
            if op not in ALL_OPS:
                groups[group]['warnings'].append('unknown native callback operation')
                continue
            current_count += 1
            if current_count > len(ALL_OPS) and group != 'legacy':
                groups[group]['warnings'].append('more than seven delegate rows in a context')
            if op in groups[group]['entries']:
                groups[group]['warnings'].append('duplicate callback entry: '+op)
                continue
            groups[group]['entries'][op] = {
                'status': info.get('status', 'unknown'),
                'module': info.get('module'),
                'callback_rva': info.get('callback_rva'),
                'original_entry_fingerprint': info.get('original_entry_fingerprint'),
            }
        if PREFIX_GATE in line:
            observations['esc_gate_count'] += 1
        if PREFIX_OPEN in line:
            info = fields(line.split(PREFIX_OPEN, 1)[1])
            if info.get('sampled') == '1':
                observations['adpcm_open_samples'] += 1
                if info.get('active') == '0':
                    observations['inactive_open_samples'] += 1
        if PREFIX_CLOSE in line:
            info = fields(line.split(PREFIX_CLOSE, 1)[1])
            if info.get('sampled') == '1' and info.get('paired') == '1':
                observations['adpcm_close_samples'] += 1
                if info.get('active') == '0':
                    observations['inactive_close_samples'] += 1
        if PREFIX_PAIR in line:
            info = fields(line.split(PREFIX_PAIR, 1)[1])
            if info.get('paired') == '1' and info.get('overlap') == '0':
                observations['paired_menu_lifetimes'] += 1
                if info.get('play_between') == '0' and info.get('stop_between') == '0':
                    observations['export_quiet_lifetimes'] += 1

    for name, group in groups.items():
        missing = [op for op in OPS if op not in group['entries']]
        missing_seven = [op for op in ALL_OPS if op not in group['entries']]
        group['missing'] = missing  # legacy core-five schema preserved
        group['missing_seven'] = missing_seven
        group['complete'] = not missing and not group['warnings']
        group['complete_seven'] = not missing_seven and not group['warnings']
        group['trusted_for_comparison'] = (group['complete'] and name != 'legacy')
        group['original_fingerprinted_operations'] = [
            op for op, entry in group['entries'].items()
            if (entry['status'] == 'resolved'
                and (entry.get('module') or '').casefold() == 'apmmxbvr.dll'
                and (entry.get('callback_rva') or '').lower()
                    == ORIGINAL_CALLBACK_RVAS[op].lower()
                and entry.get('original_entry_fingerprint') == '1')
        ]
        group['all_five_original_entry_fingerprints'] = (
            group['complete'] and name != 'legacy' and
            all(op in group['original_fingerprinted_operations'] for op in OPS))
        group['all_seven_original_entry_fingerprints'] = (
            group['complete_seven'] and name != 'legacy' and
            all(op in group['original_fingerprinted_operations'] for op in ALL_OPS))

    p, s = groups['pending'], groups['synchronized']
    if p['trusted_for_comparison'] and s['trusted_for_comparison']:
        changed = [op for op in OPS if p['entries'][op] != s['entries'][op]]
    else:
        changed = None  # never call a missing pair unchanged
    if p['complete_seven'] and s['complete_seven']:
        changed_params = [op for op in PARAM_OPS
                          if p['entries'][op] != s['entries'][op]]
    else:
        changed_params = None
    return {
        'schema': 'rayman2fix-adpcm-log-evidence-v1',
        'read_only': True,
        'observer_not_invoker': True,
        'delegates': groups,
        'stage_comparison': {
            'available': changed is not None,
            'changed_operations': changed,
            'original_entry_fingerprints_both_stages': (
                p['all_five_original_entry_fingerprints'] and
                s['all_five_original_entry_fingerprints']),
            'parameter_comparison_available': changed_params is not None,
            'changed_parameter_operations': changed_params,
            'all_seven_original_entry_fingerprints_both_stages': (
                p['all_seven_original_entry_fingerprints'] and
                s['all_seven_original_entry_fingerprints']),
            'proves_original_query_was_executed': False,
        },
        'observations': observations,
        'warnings': warnings,
        'can_prove_query_invocation': False,
        'can_prove_internal_retirement_absent': False,
        'can_prove_continuous_physical_voice_generation': False,
        'assessment': ('Pointer resolution and endpoint snapshots do not prove call execution, '
                       'absence of internal retire or immutable native voice identity. '
                       'TestIsPlaying and GetPos can themselves mutate original state.'),
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args(argv)
    try:
        content = args.log.read_text(encoding='utf-8', errors='replace')
    except OSError as exc:
        print(f'Cannot read ESC audio log: {exc}', file=sys.stderr)
        return 2
    result = classify(content.splitlines())
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for stage, group in result['delegates'].items():
            print(f'{stage}: {len(group["entries"])}/7 callbacks, '
                  f'core_complete={group["complete"]}, '
                  f'seven_complete={group["complete_seven"]}, '
                  f'missing={",".join(group["missing_seven"])}')
        print('Early/late comparison: ', result['stage_comparison'])
        print(result['assessment'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
