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



WAVMW_OWNER_SHA256 = 'cd0a0ad0a95880cd8e25e50844ec8e6f9d6725bbee7c09a8ab328cbbc953f016'
WAVMW_SITES = {
    'sxd_test_is_playing_export': (0xB8A0, '56 57 8b 7c 24 0c 8b 47 08 48'),
    'sxd_type4_branch': (0xB8AF, '83 e8 02 74 10'),
    'sxd_adpcm_case': (0xB8C4, '8b 47 04 8b 0d b0 7e 02 10 50 51'),
    'sxd_test_delegation': (0xB8CF, 'e8 bc c2 ff ff'),
    'indirect_test_vtable_call': (0x7BB0, 'ff 53 4c'),
    'vtable_destination_offset': (0x7D7E, '8d 78 2c'),
    'vtable_copy_count': (0x7DAF, 'b9 09 00 00 00'),
    'vtable_copy_instruction': (0x7DB4, 'f3 a5'),
    'vtable_setup_callback9': (0xBFA7, 'c7 44 24 2c 20 34 00 10'),
    'vtable_constructor_call': (0xBFB5, 'e8 56 bd ff ff'),
    'adpcm_test_wrapper': (0x3420, 'a1 00 6a 02 10 85 c0 75 06'),
    'adpcm_test_indirect_jump': (0x3434, 'ff 15 00 6a 02 10'),
    'dynamic_adpcm_symbol': (0x57EA, '68 60 15 02 10'),
    'dynamic_resolve': (0x57F0, 'e8 ab e7 ff ff'),
    'dynamic_pointer_published': (0x57F8, 'a3 00 6a 02 10'),
    'get_proc_address_import': (0x3FB9, 'ff 15 48 d0 01 10'),
    'callback_zero_branch': (0x7BBC, '85 ff 75 3b'),
    'wavmw_inner_invalidated': (0x7BED, 'c7 06 ff ff ff ff'),
    'wavmw_inner_free_call': (0x7BF3, 'e8 58 f9 ff ff'),
    'wavmw_outer_zero_branch': (0xB904, '85 f6 75 09'),
    'wavmw_outer_free_call': (0xB909, 'e8 22 fd ff ff'),
    'wavmw_inner_free_entry': (0x7550, '8b 44 24 04'),
    'wavmw_outer_list_unlink': (0xB630, '8b 44 24 04 33 d2'),
}
WAVMW_EDGES = {
    'sxd_test_delegation': (0xB8CF, 0x7B90),
    'vtable_constructor_call': (0xBFB5, 0x7D10),
    'dynamic_resolve': (0x57F0, 0x3FA0),
    'wavmw_inner_free_call': (0x7BF3, 0x7550),
    'wavmw_outer_free_call': (0xB909, 0xB630),
}


def audit_wavmw_bridge(data: bytes, adpcm_data: bytes) -> dict:
    """Static two-DLL type-4 bridge, *not* proof of live runtime dispatch."""
    adpcm = audit(adpcm_data)  # exact original input and no native writes
    digest = hashlib.sha256(data).hexdigest()
    if digest != WAVMW_OWNER_SHA256:
        raise PEFormatError('original WAVMW SHA-256 mismatch')
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076FB4
            or pe.image_base != 0x10000000 or pe.image_size != 0x87000):
        raise PEFormatError('original WAVMW PE metadata mismatch')
    if pe.exports().get('_SND_fn_bTestIsPlayingSxd@4') != 0xB8A0:
        raise PEFormatError('original WAVMW TestIsPlaying export mismatch')
    if PE32(adpcm_data).exports().get('_SND_fn_bTestIsPlayingSxdADPCM@4') != 0x5150:
        raise PEFormatError('original APMMX TestIsPlaying export mismatch')
    symbol = b'_SND_fn_bTestIsPlayingSxdADPCM@4\0'
    if pe.read_rva(0x21560, len(symbol)) != symbol:
        raise PEFormatError('original WAVMW ADPCM resolver symbol mismatch')
    for name, (rva, hex_bytes) in WAVMW_SITES.items():
        if pe.read_rva(rva, len(bytes.fromhex(hex_bytes))) != bytes.fromhex(hex_bytes):
            raise PEFormatError(f'original WAVMW opcode mismatch: {name}')
    for name, (site, target) in WAVMW_EDGES.items():
        opcode = pe.read_rva(site, 5)
        actual = site + 5 + int.from_bytes(opcode[1:], 'little', signed=True)
        if opcode[0] != 0xE8 or actual != target:
            raise PEFormatError(f'original WAVMW call-edge mismatch: {name}')
    if pe.read_rva(0xB8B2, 2) != b'\x74\x10':
        raise PEFormatError('original WAVMW type-4 jump mismatch')
    if 0xB8B2 + 2 + 0x10 != 0xB8C4:
        raise PEFormatError('WAVMW type-4 target mismatch')
    return {
        'wavmw_original_sha256': digest,
        'apmmx_original_sha256': adpcm['owner_sha256'],
        'wavmw_opcode_anchors_verified': len(WAVMW_SITES),
        'wavmw_direct_edges_verified': len(WAVMW_EDGES),
        'sxd_type4_test_export_rva': '0xB8A0',
        'sxd_test_vtable_dispatch_rva': '0x7B90',
        'sxd_test_dynamic_delegate_rva': '0x3420',
        'adpcm_lookup_symbol_rva': '0x21560',
        'adpcm_native_test_export_rva': '0x5150',
        'static_type4_dispatch_chain_verified': True,
        'query_can_retire_wavmw_inner_on_zero_result': True,
        'query_can_unlink_outer_sxd_node_on_zero_result': True,
        'actual_runtime_function_pointer_observed': False,
        'permission_to_change_audio': False,
        'caveat': ('WAVMW type-4 callback table and loader name statically verified on '
                   'two hash-locked owner originals; runtime pointer, callers, and '
                   'absence of external synchronization are not established.'),
    }

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_dll', type=Path)
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--wavmwbvr', type=Path, default=None,
                        help='optional exact original WAVMWbvr.dll for cross-DLL static chain')
    args = parser.parse_args(argv)
    try:
        original_bytes = args.owner_dll.read_bytes()
        result = audit(original_bytes)
        if args.wavmwbvr is not None:
            result['wavmw_bridge'] = audit_wavmw_bridge(args.wavmwbvr.read_bytes(), original_bytes)
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
