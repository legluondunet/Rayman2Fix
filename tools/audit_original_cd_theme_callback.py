#!/usr/bin/env python3
"""Read-only SHA-locked proof of the native CD Theme update callback route.

Original CD game:
   registers function 0x4596E0 with source 0x458220,
   callback conditionally runs 0x459460,
   pending +0x44 is committed to current +0x14.
No callback is invoked or hooked. This does not identify the callback
scheduler, instruction timing, audio voice epoch or per-song playback.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError
from audit_original_cd_theme_switch import (
    ORIGINAL_SHA256, ORIGINAL_PE_TIMESTAMP, ORIGINAL_IMAGE_BASE,
    ORIGINAL_IMAGE_SIZE,
)

# Original executable PE32 RVAs, VA=0x400000+RVA.
SITES = {
    'register_theme_callback_pointer': (0x59AE5, '68 e0 96 45 00'),
    'register_theme_audio_context': (0x59AE0, '68 b8 95 4b 00'),
    'register_audio_create_call': (0x59AEC, 'e8 2f e7 ff ff'),
    'native_audio_create_entry': (0x58220, '57 e8 ba f2 ff ff'),
    'native_audio_create_dispatch_type1': (0x582A5, 'e8 d6 cf ff ff'),
    'callback_entry_reads_global_gate': (0x596E0, 'a1 18 96 4b 00'),
    'callback_global_gate_conditional': (0x596E6, '85 c0 75 67'),
    'callback_active_controller_gate': (0x596EE, '83 3e 00 74 5e'),
    'callback_state_gate': (0x596F3, '8b 46 04 85 c0 75 57'),
    'callback_update_call': (0x59707, 'e8 54 fd ff ff'),
    'update_reads_pending': (0x59467, '8b 41 0c'),
    'update_reads_pending_target': (0x59473, '8b 41 44'),
    'update_clears_pending': (0x59476, '89 59 0c'),
    'update_promotes_target': (0x5948E, '89 41 14'),
    'set_pending_flag': (0x595F6, '89 41 0c'),
    'set_pending_target': (0x595FC, '89 51 44'),
}
DIRECT = {
    'player_create_registers_callback': (0x59AEC, 0x58220),
    'callback_calls_update': (0x59707, 0x59460),
    'initial_player_also_calls_update': (0x596D3, 0x59460),
    'native_create_direct_driver_entry': (0x582A5, 0x55280),
}
SHORT_BRANCHES = {
    'global_event_skip': (0x596E8, 0x59751, 0x75),
    'controller_inactive_skip': (0x596F1, 0x59751, 0x74),
    'controller_flag_skip': (0x596F8, 0x59751, 0x75),
    'update_no_pending_skip': (0x59471, 0x594B0, 0x74),
}


def audit(original_bytes: bytes) -> dict:
    digest=hashlib.sha256(original_bytes).hexdigest()
    if digest != ORIGINAL_SHA256:
        raise PEFormatError('approved original CD SHA-256 mismatch')
    pe=PE32(original_bytes)
    if (pe.machine != 0x14C or pe.timestamp != ORIGINAL_PE_TIMESTAMP or
        pe.image_base != ORIGINAL_IMAGE_BASE or
        pe.image_size != ORIGINAL_IMAGE_SIZE):
        raise PEFormatError('approved original CD PE header mismatch')
    for name,(rva,expected_hex) in SITES.items():
        want=bytes.fromhex(expected_hex)
        if pe.read_rva(rva,len(want))!=want:
            raise PEFormatError(f'original Theme callback opcode mismatch: {name}')
    for name,(site,expected) in DIRECT.items():
        raw=pe.read_rva(site,5)
        actual=site+5+int.from_bytes(raw[1:5],'little',signed=True)
        if raw[0]!=0xE8 or actual!=expected:
            raise PEFormatError(f'original Theme callback direct edge mismatch: {name}')
    for name,(site,target,opcode) in SHORT_BRANCHES.items():
        raw=pe.read_rva(site,2)
        actual=site+2+int.from_bytes(raw[1:2],'little',signed=True)
        if raw[0]!=opcode or actual!=target:
            raise PEFormatError(f'original Theme callback conditional mismatch: {name}')
    return {
        'original_sha256': digest,
        'original_pe_verified': True,
        'callback_registration_va': '0x00459AE5',
        'callback_entry_va': '0x004596E0',
        'callback_controller_va': '0x004B95B8',
        'callback_registers_with_audio_creation_va': '0x00458220',
        'callback_update_va': '0x00459460',
        'verified_opcodes': len(SITES),
        'verified_direct_call_edges': len(DIRECT),
        'verified_conditional_branches': len(SHORT_BRANCHES),
        'pending_target_committed_on_eligible_callback': True,
        'controller_global_and_state_gates_exist': True,
        'callback_invoked_during_owner_menu_proven': False,
        'global_guard_was_nonzero_during_esc_proven': False,
        'controller_state_was_nonzero_during_esc_proven': False,
        'scheduler_is_original_audio_processing_proven': False,
        'music_voice_generation_proven': False,
        'four_second_handoff_timer_proven': False,
        'runtime_audio_change_authorized': False,
        'interpretation': (
            'The original player registers a Theme update callback via '
            'the audio-create route. The callback has global and '
            'controller guards and directly calls the pending-target '
            'promotion function. The observed 3.9-second outside-ESC '
            'correlation is NOT an identified countdown or a physical '
            'voice lifetime proof. No native query or audio command '
            'was invoked by this static audit.'
        ),
    }


def main(argv=None) -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('approved_original_cd_exe',type=Path)
    ap.add_argument('--json',action='store_true')
    args=ap.parse_args(argv)
    try:
        report=audit(args.approved_original_cd_exe.read_bytes())
    except (OSError,PEFormatError,OverflowError) as exc:
        print(f'Original Theme callback audit refused: {exc}',file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report,indent=2))
    else:
        print('Verified original CD callback registration -> gated Theme '
              'update -> pending promotion.')
        print('NO-GO: not proof of callback scheduling, timer or voice generation.')
    return 0


if __name__=='__main__':
    sys.exit(main())
