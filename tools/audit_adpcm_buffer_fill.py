#!/usr/bin/env python3
"""SHA-pinned read-only original APMMX audio buffer-fill completion audit.

The original audio output/buffer-fill path can itself mark a native
ADPCM voice inactive at record+0x28, without a game call to GetPos,
TestIsPlaying, or exported Stop. This is a possible static path,
NOT an assertion that it executed in an owner's ESC menu.
No original DLL bytes included. Never executes a driver entry.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256
from audit_adpcm_lock_routes import WAVMW_OWNER_SHA256

ANCHORS = {
    'buffer_fill_entry': (0x18D0, 'a1 c4 4e 02 10 83 ec 20 85 c0 53 55 56 57 75'),
    'buffer_calls_record_progress': (0x1ACB, '8b 44 24 40 50 53 e8 6a fc ff ff'),
    'progress_entry': (0x1740, '51 53 8b 5c 24 0c 55 56 8b 43 24 57 33 d2 bf 01'),
    'progress_zero_edx': (0x174C, '33 d2'),
    'progress_field24_gate': (0x1753, '3b c2 8b 83 d8 00 00 00 89 7c 24 10 74 44'),
    'progress_d8_gate': (0x17A5, '3b c2 75 29'),
    'progress_field20_gate': (0x17BF, '8b 43 20 3b c2 0f 85 d0 00 00 00'),
    'progress_inactive_write': (0x17CA, '89 53 28'),
    'mixer_first_entry': (0x3400, 'a1 a8 82 0e 10 85 c0'),
    'mixer_first_lock': (0x342C, 'e8 ef db ff ff'),
    'mixer_first_fill_call': (0x3447, '50 57 56 6a 00 e8 7f e4 ff ff'),
    'mixer_first_unlock': (0x3498, 'e8 93 db ff ff'),
    'mixer_second_entry': (0x34B0, 'a1 a8 82 0e 10 85 c0'),
    'mixer_second_lock': (0x34CE, 'e8 4d db ff ff'),
    'mixer_second_fill_call': (0x34ED, '50 57 56 6a 01 e8 d9 e3 ff ff'),
    'mixer_second_unlock': (0x353E, 'e8 ed da ff ff'),
    'first_callback_registration_address': (0x2B39, 'c7 44 24 1c 00 34 00 10'),
    'first_callback_registration_call': (0x2B41, 'e8 0a 0f 00 00'),
    'second_callback_registration_address': (0x2B68, 'c7 44 24 1c b0 34 00 10'),
    'second_callback_registration_call': (0x2B74, 'e8 d7 0e 00 00'),
    'native_callback_registration_helper_entry': (0x3A50, 'a1 9c 84 0e 10 85 c0'),
    'native_callback_registration_indirect_call': (0x3A73, 'ff 15 9c 84 0e 10'),
    'output_provider_symbol_push': (0x4B7D, '68 10 20 01 10'),
    'output_provider_resolver_call': (0x4B83, 'e8 78 ef ff ff'),
    'output_provider_global_publication': (0x4B8B, 'a3 9c 84 0e 10'),
    'output_provider_fallback': (0x4B94, 'c7 05 9c 84 0e 10 80 3a 00 10'),
    'output_provider_fallback_returns_error': (0x3A80, '83 c8 ff c2 10 00'),
}
EDGES = {
    'progress_through_buffer_fill': (0x1AD1, 0x1740),
    'mixer_first_lock': (0x342C, 0x1020),
    'mixer_first_fill': (0x344C, 0x18D0),
    'mixer_first_unlock': (0x3498, 0x1030),
    'mixer_second_lock': (0x34CE, 0x1020),
    'mixer_second_fill': (0x34F2, 0x18D0),
    'mixer_second_unlock': (0x353E, 0x1030),
    'first_register_callback': (0x2B41, 0x3A50),
    'second_register_callback': (0x2B74, 0x3A50),
    'output_provider_lookup': (0x4B83, 0x3B00),
}
BRANCHES = {
    'progress_no_record24_skip_to_terminal': (0x175F, 0x17A5),
    'progress_d8_nonzero_skips_inactive': (0x17A7, 0x17D2),
    'progress_field20_nonzero_skips_inactive': (0x17C4, 0x189A),
}
# First branch is rel8; third rel32.
BRANCH_OPCODES = {
    'progress_no_record24_skip_to_terminal': 0x74,
    'progress_d8_nonzero_skips_inactive': 0x75,
    'progress_field20_nonzero_skips_inactive': (0x0F, 0x85),
}


WAVMW_BUFFER_SITES = {
    'wavmw_create_buffer_export': (0xC3F0, '8b 44 24 04 56 57 8b 7c 24 10'),
    'wavmw_buffer_class_gate': (0xC3FA, '83 38 01 75 2b'),
    'wavmw_backend_choice': (0xC410, '75 0c'),
    'wavmw_backend_type2_call': (0xC412, 'e8 e9 06 00 00'),
    'wavmw_backend_other_call': (0xC41E, 'e8 5d 0d 00 00'),
    'wavmw_buffer_result_storage': (0xC45C, '8b 0f 89 08 89 70 04'),
    'wavmw_buffer_error_return': (0xC468, '5f 83 c8 ff 5e c2 10 00'),
    'wavmw_type2_lower_callback_address': (0xCB88, 'c7 44 24 24 80 c9 00 10'),
    'wavmw_type2_lower_layer_buffer_call': (0xCB94, 'e8 57 f8 ff ff'),
    'wavmw_type2_indirect_callback_entry': (0xC980, '53 55 8b 6c 24 0c 56 57'),
    'wavmw_type2_indirect_callback_invocation': (0xCA25, '50 51 ff 55 18'),
}
WAVMW_BUFFER_EDGES = {
    'backend_type2_call': (0xC412, 0xCB00),
    'backend_other_call': (0xC41E, 0xD180),
    'type2_register_lower_layer_buffer': (0xCB94, 0xC3F0),
}


def audit_wavmw_registration(data: bytes) -> dict:
    """Static route for the APMMX export-provider name, not runtime execution."""
    if hashlib.sha256(data).hexdigest() != WAVMW_OWNER_SHA256:
        raise PEFormatError('original WAVMW SHA-256 mismatch')
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076FB4
            or pe.image_base != 0x10000000 or pe.image_size != 0x87000):
        raise PEFormatError('original WAVMW PE metadata mismatch')
    if pe.exports().get('_SND_fn_lCreateNewBufferExSxd@16') != 0xC3F0:
        raise PEFormatError('original WAVMW CreateNewBufferEx export mismatch')
    for name,(rva,hex_bytes) in WAVMW_BUFFER_SITES.items():
        expected=bytes.fromhex(hex_bytes)
        if pe.read_rva(rva,len(expected)) != expected:
            raise PEFormatError(f'original WAVMW buffer callback opcode mismatch: {name}')
    for name,(rva,target) in WAVMW_BUFFER_EDGES.items():
        code=pe.read_rva(rva,5)
        actual=rva+5+int.from_bytes(code[1:5],'little',signed=True)
        if code[0]!=0xE8 or actual!=target:
            raise PEFormatError(f'original WAVMW backend call edge mismatch: {name}')
    return {
        'original_wavmw_sha256': WAVMW_OWNER_SHA256,
        'wavmw_buffer_create_export_rva': '0xC3F0',
        'wavmw_buffer_class_gate_rva': '0xC3FA',
        'wavmw_backend_for_type2_rva': '0xCB00',
        'wavmw_backend_for_other_types_rva': '0xD180',
        'opcode_windows_verified': len(WAVMW_BUFFER_SITES),
        'direct_call_edges_verified': len(WAVMW_BUFFER_EDGES),
        'original_callback_provider_link_verified': True,
        'wavmw_type2_can_wrap_lower_layer_callback': True,
        'wamw_indirect_callback_target_generation_proven': False,
        'runtime_provider_selected_or_invoked_proven': False,
        'callback_executed_during_esc_proven': False,
        'permission_to_change_audio': False,
    }


def audit(data: bytes) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if digest != OWNER_ORIGINAL_SHA256:
        raise PEFormatError('original APMMX SHA-256 mismatch')
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076F98
            or pe.image_base != 0x10000000 or pe.image_size != 0xEE000):
        raise PEFormatError('original APMMX PE metadata mismatch')
    provider_symbol = b'_SND_fn_lCreateNewBufferExSxd@16\0'
    if pe.read_rva(0x12010, len(provider_symbol)) != provider_symbol:
        raise PEFormatError('original audio output provider dynamic symbol mismatch')
    for name, (rva, hex_bytes) in ANCHORS.items():
        expected = bytes.fromhex(hex_bytes)
        if pe.read_rva(rva, len(expected)) != expected:
            raise PEFormatError(f'original mixer opcode mismatch: {name}')
    for name, (rva, target) in EDGES.items():
        code = pe.read_rva(rva, 5)
        if (code[0] != 0xE8 or
                rva + 5 + int.from_bytes(code[1:5], 'little', signed=True) != target):
            raise PEFormatError(f'original mixer call edge mismatch: {name}')
    for name, (rva, target) in BRANCHES.items():
        op = BRANCH_OPCODES[name]
        if isinstance(op, int):
            raw = pe.read_rva(rva, 2)
            valid = raw[0] == op
            actual = rva + 2 + int.from_bytes(raw[1:2], 'little', signed=True)
        else:
            raw = pe.read_rva(rva, 6)
            valid = raw[:2] == bytes(op)
            actual = rva + 6 + int.from_bytes(raw[2:6], 'little', signed=True)
        if not valid or actual != target:
            raise PEFormatError(f'original mixer conditional edge mismatch: {name}')
    return {
        'original_sha256': digest,
        'verified_opcode_windows': len(ANCHORS),
        'verified_direct_call_edges': len(EDGES),
        'verified_conditional_branches': len(BRANCHES),
        'buffer_fill_rva': '0x18D0',
        'buffer_progress_rva': '0x1740',
        'native_active_zero_write_rva': '0x17CA',
        'normal_buffer_fill_can_mark_voice_inactive': True,
        'original_two_callbacks_registered_through_indirect_output_interface': True,
        'dynamic_output_provider_symbol': '_SND_fn_lCreateNewBufferExSxd@16',
        'dynamic_output_provider_resolution_proven': True,
        'specific_runtime_provider_module_observed': False,
        'actual_callback_execution_during_esc_observed': False,
        'registered_callbacks_always_run_on_a_separate_thread_proven': False,
        'mixer_wrappers_hold_known_driver_lock': True,
        'getpos_testisplaying_stop_required_for_this_write': False,
        'occurred_during_owners_first_esc_proven': False,
        'native_retirement_or_new_physical_generation_proven': False,
        'permission_to_hook_or_change_audio': False,
        'caveat': ('Original mixer buffer fill has a conditional active=0 write. '
                   'Actual runtime execution/thread, handle retirement and '
                   'external synchronization not observed. This tool does not '
                   'call or instrument the native driver.'),
    }


@dataclass(frozen=True)
class ProgressResult:
    activity_may_be_cleared: bool
    physical_retirement_proven: bool = False


def model_progress(*, field24_zero: bool, field_d8_zero: bool,
                   field20_zero: bool) -> ProgressResult:
    """Synthetic branch model, NOT a claim of a particular game event."""
    if any(type(v) is not bool for v in
           (field24_zero, field_d8_zero, field20_zero)):
        raise ValueError('missing original progression gate')
    return ProgressResult(field24_zero and field_d8_zero and field20_zero)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original_apmmx_dll', type=Path)
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--wavmwbvr', type=Path, default=None,
                        help='optional exact original WAVMW for complete cross-DLL buffer creation route')
    args = parser.parse_args(argv)
    try:
        result = audit(args.original_apmmx_dll.read_bytes())
        if args.wavmwbvr is not None:
            result['wavmw_creation_route'] = audit_wavmw_registration(args.wavmwbvr.read_bytes())
    except (OSError, PEFormatError, OverflowError) as exc:
        print(f'APMMX mixer completion audit refused: {exc}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print('Original buffer-fill path verified; can conditionally set active=0')
        print('NO-GO: not evidence of execution or of a stable physical voice generation')
    return 0


if __name__ == '__main__':
    sys.exit(main())
