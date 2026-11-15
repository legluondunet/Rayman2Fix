#!/usr/bin/env python3
"""Original APMMXbvr: inspect published Play/Stop and hidden retire chains.

Research only. Verifies exact SHA/PE, opcode anchors, relative control
flow and direct references to the original critical-section callbacks.
A byte scan does not prove all caller-side or indirect synchronization.
Never loads or modifies the original module.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256

# All RVAs from owner's exact 1999 PE32 DLL. These are instruction
# signatures, not original executable code distributed in this repo.
ANCHORS = {
    "export_test_is_playing": (0x5150, "8b 44 24 04 50 e8 a6 d7 ff ff 83 c4 04 c2 04 00"),
    "test_is_playing_inner": (0x2900, "a1 a8 82 0e 10 85 c0 74 17"),
    "test_calls_cleanup": (0x290E, "e8 3d f6 ff ff"),
    "cleanup_inspects_active": (0x1F5A, "8b 51 28 85 d2 74 06"),
    "cleanup_can_retire": (0x1F68, "e8 63 f6 ff ff"),
    "transition_play_entry": (0x2780, "51 a1 a8 82 0e 10 56 85 c0 57"),
    "transition_play_acquire": (0x2795, "e8 86 e8 ff ff"),
    "transition_play_allocates": (0x27BE, "e8 dd eb ff ff"),
    "transition_play_release": (0x281D, "e8 0e e8 ff ff"),
    "regular_play_entry": (0x31E0, "51 a1 a8 82 0e 10"),
    "regular_play_acquire": (0x31F7, "e8 24 de ff ff"),
    "regular_play_allocates": (0x3223, "e8 78 e1 ff ff"),
    "regular_play_release": (0x323C, "e8 ef dd ff ff"),
    "direct_stop_entry": (0x2930, "a1 a8 82 0e 10 56"),
    "direct_stop_acquire": (0x2947, "e8 d4 e6 ff ff"),
    "direct_stop_retire": (0x2950, "e8 7b ec ff ff"),
    "direct_stop_release": (0x295D, "e8 ce e6 ff ff"),
    "transition_stop_entry": (0x3340, "a1 a8 82 0e 10 85 c0"),
    "transition_stop_acquire": (0x334E, "e8 cd dc ff ff"),
    "transition_stop_inner": (0x335B, "e8 a0 e3 ff ff"),
    "transition_stop_release": (0x3368, "e8 c3 dc ff ff"),
}
# Known direct E8+rel32 transfer edges in the original file.
EDGES = {
    "export_test_is_playing": (0x5155, 0x2900),
    "test_calls_cleanup": (0x290E, 0x1F50),
    "cleanup_can_retire": (0x1F68, 0x15D0),
    "transition_play_acquire": (0x2795, 0x1020),
    "transition_play_allocates": (0x27BE, 0x13A0),
    "transition_play_release": (0x281D, 0x1030),
    "regular_play_acquire": (0x31F7, 0x1020),
    "regular_play_allocates": (0x3223, 0x13A0),
    "regular_play_release": (0x323C, 0x1030),
    "direct_stop_acquire": (0x2947, 0x1020),
    "direct_stop_retire": (0x2950, 0x15D0),
    "direct_stop_release": (0x295D, 0x1030),
    "transition_stop_acquire": (0x334E, 0x1020),
    "transition_stop_inner": (0x335B, 0x1700),
    "transition_stop_release": (0x3368, 0x1030),
}
# Original bounded function ranges (end-exclusive). Raw scan explicitly
# checks only direct E8 calls to the known lock handlers, not all x86 paths.
NO_LOCAL_LOCK = ((0x5150, 0x515F), (0x2900, 0x2923),
                 (0x1F50, 0x1F73), (0x15D0, 0x16F1))
LOCKED_ROUTES = {
    "transition_play": (0x2780, 0x283D),
    "regular_play": (0x31E0, 0x324A),
    "direct_stop": (0x2930, 0x2967),
    "transition_stop": (0x3340, 0x3371),
}


def direct_rel32_candidates(data: bytes, start: int, target: int) -> tuple[int, ...]:
    """Raw direct E8 candidates; possible false positives inside operands/data."""
    hits = []
    for offset in range(max(0, len(data) - 4)):
        if data[offset] != 0xE8:
            continue
        disp = int.from_bytes(data[offset + 1:offset + 5], 'little', signed=True)
        if start + offset + 5 + disp == target:
            hits.append(start + offset)
    return tuple(hits)


def audit(data: bytes) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if digest != OWNER_ORIGINAL_SHA256:
        raise PEFormatError('original APMMX SHA-256 mismatch')
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076F98
            or pe.image_base != 0x10000000 or pe.image_size != 0xEE000):
        raise PEFormatError('original APMMX PE metadata mismatch')
    for name, (rva, expected_hex) in ANCHORS.items():
        expected = bytes.fromhex(expected_hex)
        if pe.read_rva(rva, len(expected)) != expected:
            raise PEFormatError(f'original opcode mismatch: {name} at 0x{rva:X}')
    for name, (site, target) in EDGES.items():
        opcode = pe.read_rva(site, 5)
        actual = site + 5 + int.from_bytes(opcode[1:], 'little', signed=True)
        if opcode[0] != 0xE8 or actual != target:
            raise PEFormatError(f'original call edge mismatch: {name}')
    for start, end in NO_LOCAL_LOCK:
        block = pe.read_rva(start, end - start)
        for lock in (0x1020, 0x1030):
            if direct_rel32_candidates(block, start, lock):
                raise PEFormatError(f'unexpected direct lock call in test/cleanup path at 0x{start:X}')
    locked = {}
    for name, (start, end) in LOCKED_ROUTES.items():
        block = pe.read_rva(start, end - start)
        enters = direct_rel32_candidates(block, start, 0x1020)
        leaves = direct_rel32_candidates(block, start, 0x1030)
        if len(enters) != 1 or len(leaves) != 1 or enters[0] >= leaves[0]:
            raise PEFormatError(f'known original lock envelope mismatch: {name}')
        locked[name] = {'enter_rva': f'0x{enters[0]:04X}',
                        'leave_rva': f'0x{leaves[0]:04X}'}
    return {
        'owner_sha256': digest,
        'opcode_anchors_verified': len(ANCHORS),
        'relative_call_edges_verified': len(EDGES),
        'local_lockless_ranges_verified': len(NO_LOCAL_LOCK),
        'known_locked_routes': locked,
        'exported_test_is_playing_rva': '0x5150',
        'internal_inactive_cleanup_rva': '0x1F50',
        'retirement_rva': '0x15D0',
        'known_lock_va': '0x100163D0',
        'exported_stop_count_cannot_prove_no_retirement': True,
        'all_retirement_synchronized_proven': False,
        'runtime_audio_change_authorized': False,
        'limitation': ('The exported TestIsPlaying -> cleanup -> retirement path has '
                       'no direct acquisition of the driver lock in these four '
                       'functions. This does not establish the state of external '
                       'serialization or indirect calls. No physical voice epoch '
                       'or safe native intercept has been proved.'),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_dll', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        result = audit(args.owner_dll.read_bytes())
    except (OSError, PEFormatError, OverflowError) as exc:
        print(f'APMMX route audit refused: {exc}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"Verified {result['opcode_anchors_verified']} original opcode windows and "
              f"{result['relative_call_edges_verified']} call edges")
        print('Original TestIsPlaying can retire an inactive record without '
              'calling the exported Stop and without a local direct critical-section enter.')
        print('NO-GO: a zero exported Stop count cannot certify a stable voice generation.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
