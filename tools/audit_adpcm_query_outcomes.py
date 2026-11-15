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
        'caveat': ('Static original-DLL outcome classification only. Native '
                   'TestIsPlaying can itself mutate state; never call it as an observer.'),
    }

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
