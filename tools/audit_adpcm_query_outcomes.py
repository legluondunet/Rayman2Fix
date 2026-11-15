#!/usr/bin/env python3
"""Original Rayman 2 WAVMW/APMMX TestIsPlaying branch classification.

Research-only, SHA-pinned and read-only. Native TestIsPlaying is a
side-effecting query (it may retire a voice), NEVER execute it merely to
observe. A false query result alone proves neither a callback nor retire.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError
from audit_adpcm_lock_routes import audit_wavmw_bridge

# Exact original x86 instruction windows. Proprietary DLLs are not included.
WAVMW_GATES = {
    'inner_field34_test': (0x7B9B, '8b 46 34 85 c0 74 04'),
    'inner_field34_forced_false': (0x7BA2, '33 ff eb 16'),
    'inner_field30_test': (0x7BA6, '8b 46 30 85 c0 74 0a'),
    'inner_native_dispatch': (0x7BAD, '8b 06 50 ff 53 4c 8b f8 eb 05'),
    'inner_not_started_is_true': (0x7BB7, 'bf 01 00 00 00'),
    'inner_zero_result_cleanup': (0x7BBC, '85 ff 75 3b'),
    'outer_zero_result_cleanup': (0xB904, '85 f6 75 09 57 e8 22 fd ff ff'),
    'resolver_fallback_branch': (0x57FD, '85 c0 75 0a'),
    'resolver_fallback_target': (0x5801, 'c7 05 00 6a 02 10 40 34 00 10'),
    'fallback_returns_false': (0x3440, '33 c0 c2 04 00'),
}
APMMX_GATES = {
    'initialization_check': (0x2900, 'a1 a8 82 0e 10 85 c0 74 17'),
    'native_delegate_test_result': (0x2916, '85 c0 74 06'),
    'native_result_true': (0x291A, 'b8 01 00 00 00 c3'),
    'native_result_false': (0x2920, '33 c0 c3'),
    'inner_null_handle_check': (0x1F50, '8b 44 24 04 85 c0 74 18'),
    'native_record_active_check': (0x1F58, '8b 08 8b 51 28 85 d2 74 06'),
    'native_record_active_true': (0x1F61, 'b8 01 00 00 00 c3'),
    'inactive_record_retire_call': (0x1F67, '50 e8 63 f6 ff ff'),
    'native_cleanup_false': (0x1F70, '33 c0 c3'),
}
SHORT_JUMPS = {
    'inner_field34_zero_goes_to_mode': ('wav', 0x7BA0, 0x7BA6),
    'inner_field34_forced_false_goes_to_cleanup': ('wav', 0x7BA4, 0x7BBC),
    'inner_field30_zero_skips_native': ('wav', 0x7BAB, 0x7BB7),
    'native_resolver_failure_uses_stub': ('wav', 0x57FF, 0x580B),
    'apmmx_disabled_returns_false': ('apm', 0x2907, 0x2920),
    'apmmx_null_handle_returns_false': ('apm', 0x1F56, 0x1F70),
    'apmmx_inactive_record_calls_retire': ('apm', 0x1F5F, 0x1F67),
}

def audit(wav_data: bytes, apm_data: bytes) -> dict:
    # The prior independent audit verifies both exact SHA-256 hashes,
    # original export names, callback table and inactive cleanup routes.
    bridge = audit_wavmw_bridge(wav_data, apm_data)
    wav, apm = PE32(wav_data), PE32(apm_data)
    for pe, sites in ((wav, WAVMW_GATES), (apm, APMMX_GATES)):
        for name, (rva, hex_bytes) in sites.items():
            opcode = bytes.fromhex(hex_bytes)
            if pe.read_rva(rva, len(opcode)) != opcode:
                raise PEFormatError(f'query gate opcode mismatch: {name}')
    for name, (dll, rva, target) in SHORT_JUMPS.items():
        b = (wav if dll == 'wav' else apm).read_rva(rva, 2)
        if b[0] not in (0x74, 0x75, 0xEB):
            raise PEFormatError(f'query jump opcode mismatch: {name}')
        displacement = int.from_bytes(b[1:], 'little', signed=True)
        if rva + 2 + displacement != target:
            raise PEFormatError(f'query jump target mismatch: {name}')
    return {
        'original_wavmw_sha256': hashlib.sha256(wav_data).hexdigest(),
        'original_apmmx_sha256': hashlib.sha256(apm_data).hexdigest(),
        'static_owner_bridge_verified': bridge['static_type4_dispatch_chain_verified'],
        'wavmw_query_gates_verified': len(WAVMW_GATES),
        'apmmx_query_gates_verified': len(APMMX_GATES),
        'conditional_branch_targets_verified': len(SHORT_JUMPS),
        'false_outcome_can_skip_apmmx_callback': True,
        'false_outcome_can_trigger_apmmx_retirement_attempt': True,
        'false_outcome_proves_completed_retirement': False,
        'zero_exported_stop_count_proves_no_retire': False,
        'permission_to_modify_audio': False,
        'runtime_callback_observed': False,
        'getpos_side_effect': audit_position_getter(apm),
        'wavmw_getpos_bridge': audit_wavmw_position_bridge(wav, apm),
        'wavmw_setparam_bridge': audit_wavmw_setparam_bridge(wav, apm),
        'caveat': ('Static original-DLL outcome classification only. Native '
                   'TestIsPlaying can itself mutate state; never call it as an observer.'),
    }


# _SND_fn_rGetPosSxdADPCM@4 (RVA 0x51A0) is a query with
# a *conditional write* to the SAME original +0x28 active flag
# that bTestIsPlaying's internal cleanup later inspects.
# All opcode windows checked on the exact owner original.

# The game-facing WAVMW _SND_fn_rGetPosSxd@4 export has its own
# type-4 branch to the exact APMMX position getter. This bridge,
# too, can be absent at runtime; the gate is not automatically taken.
WAVMW_GETPOS_SITES = {
    'game_facing_getpos': (0xBD20, '8b 4c 24 04 8b 41 08 48 74 3c'),
    'type4_getpos_branch': (0xBD2D, '83 e8 02 74 12'),
    'type4_getpos_dispatch': (0xBD44, '8b 41 04 8b 0d b0 7e 02 10 50 51 e8 7c bf ff ff'),
    'getpos_checks_inner_started': (0x7CD0, '8b 44 24 08 8b 48 30 85 c9 74 0b'),
    'getpos_indirect_callback_slot': (0x7CE2, 'ff 51 40'),
    'getpos_uninitialized_returns_minus_one': (0x7CE6, '83 c8 ff c3'),
    'type4_callback_table_slot6': (0xBF8F, 'c7 44 24 20 10 35 00 10'),
    'type4_callback_table_copy': (0x7DAF, 'b9 09 00 00 00 f3 a5'),
    'getpos_dynamic_trampoline': (0x3510, 'a1 14 6a 02 10 85 c0 75 06'),
    'getpos_dynamic_callback': (0x3524, 'ff 15 14 6a 02 10'),
    'loader_getpos_symbol_pointer': (0x587E, '68 08 16 02 10 56 e8 17 e7 ff ff'),
    'loader_getpos_pointer_publish': (0x588C, 'a3 14 6a 02 10'),
    'loader_getpos_fallback': (0x5895, 'c7 05 14 6a 02 10 30 35 00 10'),
}
WAVMW_GETPOS_EDGES = {
    'type4_to_position_helper': (0xBD4F, 0x7CD0),
    'symbol_to_kernel_resolver': (0x5884, 0x3FA0),
}


def audit_wavmw_position_bridge(wav: PE32, apm: PE32) -> dict:
    if wav.exports().get('_SND_fn_rGetPosSxd@4') != 0xBD20:
        raise PEFormatError('original WAVMW GetPosSxd export mismatch')
    if apm.exports().get('_SND_fn_rGetPosSxdADPCM@4') != 0x51A0:
        raise PEFormatError('original APMMX GetPosSxd export mismatch')
    symbol = b'_SND_fn_rGetPosSxdADPCM@4\0'
    if wav.read_rva(0x21608, len(symbol)) != symbol:
        raise PEFormatError('original WAVMW GetPos ADPCM symbol mismatch')
    for name, (rva, raw_hex) in WAVMW_GETPOS_SITES.items():
        raw = bytes.fromhex(raw_hex)
        if wav.read_rva(rva, len(raw)) != raw:
            raise PEFormatError(f'original WAVMW GetPos opcode mismatch: {name}')
    for name, (site, target) in WAVMW_GETPOS_EDGES.items():
        raw = wav.read_rva(site, 5)
        calculated = site + 5 + int.from_bytes(raw[1:], 'little', signed=True)
        if raw[0] != 0xE8 or calculated != target:
            raise PEFormatError(f'original WAVMW GetPos direct-call mismatch: {name}')
    branch = wav.read_rva(0xBD30, 2)
    if branch[0] != 0x74 or 0xBD32 + int.from_bytes(branch[1:], 'little', signed=True) != 0xBD44:
        raise PEFormatError('original WAVMW GetPos type-4 branch target mismatch')
    return {
        'game_facing_getpos_export_rva': '0xBD20',
        'adpcm_getpos_export_rva': '0x51A0',
        'wavmw_opcode_windows_verified': len(WAVMW_GETPOS_SITES),
        'wavmw_direct_edges_verified': len(WAVMW_GETPOS_EDGES),
        'adpcm_getpos_symbol_rva': '0x21608',
        'wavmw_getpos_callback_slot': '0x40',
        'dynamic_function_pointer_rva': '0x26A14',
        'type4_adpcm_dispatch_path_verified': True,
        'uninitialized_inner_returns_minus_one_without_adpcm': True,
        'runtime_dynamic_pointer_currently_observed': False,
        'getpos_called_during_esc_proven': False,
        'runtime_audio_change_authorized': False,
    }

# Two additional original game-facing parameter routes. A successful
# exported SetParam call is NOT a Stop, but the native APMMX helper
# may internally retire a handle if its record was already inactive.
# This verification is static and never runs any driver callback.
WAVMW_SETPARAM_SITES = {
    'plain_export': (0xB7F0, '83 ec 08 56 57 8b 7c 24 14'),
    'plain_type4_branch': (0xB802, '83 e8 02 74 10'),
    'plain_type4_call': (0xB827, 'e8 c4 c2 ff ff'),
    'plain_on_false_unlinks_outer': (0xB87A, '85 f6 75 09 57 e8 ac fd ff ff'),
    'plain_indirect_parameter_callback': (0x7B12, '8b 06 57 50 ff 55 34'),
    'plain_callback_table_entry': (0xBF77, 'c7 44 24 14 e0 33 00 10'),
    'plain_dynamic_trampoline': (0x33E0, 'a1 fc 69 02 10 85 c0 75 06'),
    'plain_dynamic_indirect_call': (0x33F9, 'ff 15 fc 69 02 10'),
    'plain_resolver_symbol': (0x57C5, '68 40 15 02 10'),
    'plain_resolver_call': (0x57CB, 'e8 d0 e7 ff ff'),
    'plain_resolver_publish': (0x57D3, 'a3 fc 69 02 10'),
    'plain_resolver_fallback': (0x57DC, 'c7 05 fc 69 02 10 10 34 00 10'),
    'transition_export': (0xC5D0, '83 ec 08 56 8b 74 24 10 8b 06'),
    'transition_type4_branch': (0xC5E0, '83 e8 02 74 15'),
    'transition_type4_call': (0xC603, 'e8 28 75 ff ff'),
    'transition_dynamic_trampoline': (0x3B30, 'a1 88 6a 02 10 85 c0 75 06'),
    'transition_dynamic_indirect_call': (0x3B49, 'ff 15 88 6a 02 10'),
    'transition_resolver_symbol': (0x5C8A, '68 20 1a 02 10'),
    'transition_resolver_call': (0x5C90, 'e8 0b e3 ff ff'),
    'transition_resolver_publish': (0x5C98, 'a3 88 6a 02 10'),
    'transition_resolver_fallback': (0x5CA1, 'c7 05 88 6a 02 10 60 3b 00 10'),
}
WAVMW_SETPARAM_EDGES = {
    'plain_type4_helper': (0xB827, 0x7AF0),
    'plain_false_cleanup': (0xB87F, 0xB630),
    'transition_type4_helper': (0xC603, 0x3B30),
    'plain_symbol_resolver': (0x57CB, 0x3FA0),
    'transition_symbol_resolver': (0x5C90, 0x3FA0),
}
WAVMW_SETPARAM_BRANCHES = {
    'plain_type4_entry': (0xB805, 0xB817),
    'transition_type4_entry': (0xC5E3, 0xC5FA),
}


def audit_wavmw_setparam_bridge(wav: PE32, apm: PE32) -> dict:
    """Two original parameter routes; no actual native operation observed."""
    wav_exports = wav.exports()
    apm_exports = apm.exports()
    names = (
        ('_SND_fn_bSetParamSxd@8', 0xB7F0,
         '_SND_fn_bSetParamSxdADPCM@8', 0x5120,
         0x21540, 0x269FC, 0x3410),
        ('_SND_fn_bSetParamTransitionSxd@8', 0xC5D0,
         '_SND_fn_bSetParamTransitionSxdADPCM@8', 0x5430,
         0x21A20, 0x26A88, 0x3B60),
    )
    for wn, wr, an, ar, symbol_rva, slot_rva, fallback_rva in names:
        if wav_exports.get(wn) != wr or apm_exports.get(an) != ar:
            raise PEFormatError('original SetParam export identity mismatch')
        symbol = an.encode('ascii') + b'\0'
        if wav.read_rva(symbol_rva, len(symbol)) != symbol:
            raise PEFormatError('original SetParam dynamic symbol mismatch')
        if not (0 < slot_rva < 0x87000 and 0 < fallback_rva < 0x87000):
            raise PEFormatError('SetParam pointer layout unapproved')
    for name, (rva, hex_bytes) in WAVMW_SETPARAM_SITES.items():
        expected = bytes.fromhex(hex_bytes)
        if wav.read_rva(rva, len(expected)) != expected:
            raise PEFormatError(f'original WAVMW SetParam opcode mismatch: {name}')
    for name, (site, target) in WAVMW_SETPARAM_EDGES.items():
        b = wav.read_rva(site, 5)
        found = site + 5 + int.from_bytes(b[1:], 'little', signed=True)
        if b[0] != 0xE8 or found != target:
            raise PEFormatError(f'original WAVMW SetParam call edge mismatch: {name}')
    for name, (site, target) in WAVMW_SETPARAM_BRANCHES.items():
        b = wav.read_rva(site, 2)
        found = site + 2 + int.from_bytes(b[1:], 'little', signed=True)
        if b[0] != 0x74 or found != target:
            raise PEFormatError(f'original WAVMW SetParam branch mismatch: {name}')
    return {
        'wavmw_opcode_anchors_verified': len(WAVMW_SETPARAM_SITES),
        'wavmw_direct_call_edges_verified': len(WAVMW_SETPARAM_EDGES),
        'wavmw_conditional_branches_verified': len(WAVMW_SETPARAM_BRANCHES),
        'game_facing_setparam_export_rva': '0xB7F0',
        'game_facing_setparam_transition_export_rva': '0xC5D0',
        'adpcm_setparam_export_rva': '0x5120',
        'adpcm_setparam_transition_export_rva': '0x5430',
        'plain_setparam_dynamic_slot_rva': '0x269FC',
        'transition_setparam_dynamic_slot_rva': '0x26A88',
        'plain_setparam_can_unlink_wavmw_outer_on_false': True,
        'hidden_apmmx_retirement_during_esc_observed': False,
        'native_query_or_setter_was_executed': False,
        'runtime_audio_change_authorized': False,
    }


APMMX_POSITION_SITES = {
    'getpos_export': (0x51A0, '8b 44 24 04 50 e8 56 d8 ff ff 83 c4 04 c2 04 00'),
    'getpos_core': (0x2A00, 'a1 a8 82 0e 10 83 ec 08 56'),
    'getpos_calls_reader': (0x2A1D, '50 51 52 e8 fb f8 ff ff'),
    'reader_entry': (0x2320, '83 ec 08 53 55 8b 6c 24 18'),
    'reader_validates_handle': (0x2353, '50 56 e8 26 01 00 00'),
    'reader_zero_register': (0x238C, '33 db'),
    'reader_condition_dc': (0x2405, '39 9e dc 00 00 00 74 3c'),
    'reader_threshold': (0x243C, 'f6 c4 01 dd 5d 00 dd d8 75 03'),
    'reader_inactive_write': (0x2446, '89 5e 28'),
    'reader_compare_inactive': (0x244E, '39 5e 28'),
    'reader_terminal_position_check': (0x2449, '3b 4e 4c 75 14'),
    'reader_terminal_inactive_check': (0x244E, '39 5e 28 75 0f'),
    'reader_terminal_status_15': (0x2453, 'bf 0f 00 00 00'),
    'getter_status_15_maps_minus_two': (0x2A2C, '83 f8 0f 75 1d be fe ff ff ff'),
    'getter_default_error_minus_one': (0x2A09, '83 ce ff'),
    'normal_playback_can_inactivate': (0x17CA, '89 53 28'),
    'primary_initializer_sets_active': (0x2007, 'c7 40 28 01 00 00 00'),
}
APMMX_POSITION_EDGES = {
    'getpos_export_to_core': (0x51A5, 0x2A00),
    'getpos_core_to_reader': (0x2A20, 0x2320),
    'reader_to_handle_validator': (0x2355, 0x2480),
}
APMMX_POSITION_SHORT_BRANCHES = {
    'no_dc_skip_mutation': (0x240B, 0x2449),
    'fpu_status_skip_mutation': (0x2444, 0x2449),
}


def audit_position_getter(apm: PE32) -> dict:
    """Proof of a possible query-side write, not that a real game call did so."""
    if apm.exports().get('_SND_fn_rGetPosSxdADPCM@4') != 0x51A0:
        raise PEFormatError('original ADPCM GetPosSxd export mismatch')
    for name, (rva, raw_hex) in APMMX_POSITION_SITES.items():
        raw = bytes.fromhex(raw_hex)
        if apm.read_rva(rva, len(raw)) != raw:
            raise PEFormatError(f'GetPos original opcode mismatch: {name}')
    for name, (site, expected_target) in APMMX_POSITION_EDGES.items():
        raw = apm.read_rva(site, 5)
        actual = site + 5 + int.from_bytes(raw[1:], 'little', signed=True)
        if raw[0] != 0xE8 or actual != expected_target:
            raise PEFormatError(f'GetPos original direct-call mismatch: {name}')
    for name, (site, expected_target) in APMMX_POSITION_SHORT_BRANCHES.items():
        raw = apm.read_rva(site, 2)
        actual = site + 2 + int.from_bytes(raw[1:], 'little', signed=True)
        if raw[0] not in (0x74, 0x75) or actual != expected_target:
            raise PEFormatError(f'GetPos original conditional-branch mismatch: {name}')
    return {
        'getter_export_rva': '0x51A0',
        'internal_position_reader_rva': '0x2320',
        'conditional_record_active_clear_rva': '0x2446',
        'reinitialization_active_set_rva': '0x2007',
        'other_inactive_writer_rva': '0x17CA',
        'opcode_windows_verified': len(APMMX_POSITION_SITES),
        'direct_call_edges_verified': len(APMMX_POSITION_EDGES),
        'conditional_branches_verified': len(APMMX_POSITION_SHORT_BRANCHES),
        'getpos_is_not_guaranteed_pure': True,
        'terminal_reader_status_15_returned_by_getter_as_minus_two': True,
        'terminal_status_is_not_retirement': True,
        'getpos_will_always_make_voice_inactive': False,
        'getpos_called_during_owners_esc_log_proven': False,
        'completed_driver_retirement_proven': False,
        'runtime_audio_change_authorized': False,
    }


def classify_original_position_status(reader_status: int) -> str:
    """Conservative status mapping of exact APMMX GetPos wrapper.

    Original 0x2A2C..0x2A31 maps native reader status 15 to -2.
    Status zero returns a computed position; other errors map to -1.
    The computed position and successful physical retirement are NOT
    derivable from the status alone.
    """
    if type(reader_status) is not int or not 0 <= reader_status <= 0xffffffff:
        raise ValueError('unknown native GetPos reader status')
    if reader_status == 15:
        return 'completed-sentinel-minus-two-not-retired'
    if reader_status == 0:
        return 'position-computed-not-proof-of-lifetime'
    return 'error-minus-one-not-proof-of-lifetime'


@dataclass(frozen=True)
class PositionOutcome:
    active_after: bool
    active_flag_cleared: bool
    native_retirement_proven: bool = False


def model_position(*, active_before: bool, gate_dc_nonzero: bool,
                   fpu_comparison_skips: bool) -> PositionOutcome:
    """Only the audited +0xDC/conditional-active-clear path; not playback."""
    if any(type(x) is not bool for x in
           (active_before, gate_dc_nonzero, fpu_comparison_skips)):
        raise ValueError('unknown position-query branch provenance')
    should_clear = gate_dc_nonzero and not fpu_comparison_skips
    return PositionOutcome(active_before and not should_clear,
                           active_before and should_clear)

@dataclass(frozen=True)
class Outcome:
    wavmw_result: int
    apmmx_callback_entered: bool
    native_retirement_attempt: bool
    wavmw_inner_cleanup: bool
    wavmw_outer_cleanup: bool
    verified_physical_retirement: bool = False

def model(*, field34_nonzero: bool, field30_nonzero: bool,
          delegate_resolved: bool | None = None,
          adpcm_initialized: bool | None = None,
          handle_present: bool | None = None,
          record_active: bool | None = None) -> Outcome:
    """Synthetic, conservative decision model; no original audio is called."""
    if type(field34_nonzero) is not bool or type(field30_nonzero) is not bool:
        raise ValueError('WAVMW flags must be known bools')
    if field34_nonzero:
        return Outcome(0, False, False, True, True)
    if not field30_nonzero:
        return Outcome(1, False, False, False, False)
    if type(delegate_resolved) is not bool:
        raise ValueError('missing resolver provenance')
    if not delegate_resolved:
        return Outcome(0, False, False, True, True)
    if type(adpcm_initialized) is not bool:
        raise ValueError('missing ADPCM initialization evidence')
    if not adpcm_initialized:
        return Outcome(0, True, False, True, True)
    if type(handle_present) is not bool:
        raise ValueError('missing handle evidence')
    if not handle_present:
        return Outcome(0, True, False, True, True)
    if type(record_active) is not bool:
        raise ValueError('missing record-active evidence')
    if record_active:
        return Outcome(1, True, False, False, False)
    # Only the entry into original retirement is established. Its
    # internal validator can still refuse the handle.
    return Outcome(0, True, True, True, True)

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original_wavmwbvr', type=Path)
    parser.add_argument('original_apmmxbvr', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        report = audit(args.original_wavmwbvr.read_bytes(),
                       args.original_apmmxbvr.read_bytes())
    except (OSError, PEFormatError, OverflowError) as exc:
        print(f'ADPCM query outcome audit refused: {exc}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print('Original DLLs: query bypass, callback and cleanup paths verified')
        print('NO-GO: a false TestIsPlaying result does not prove physical voice retirement')
    return 0

if __name__ == '__main__':
    sys.exit(main())
