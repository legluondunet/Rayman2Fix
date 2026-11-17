#!/usr/bin/env python3
"""Read-only SHA-locked original Rayman 2 CD ESC sound-table walk audit.

The 1999 source script Pause/Resume wrappers iterate exactly 64
32-byte original sound records. An offline preflight can reproduce
the source-table eligibility predicate *at a moment in time*, but
cannot atomically bind the future native ADPCM physical generation.

No game/dll imports, exported callbacks, hooks or process memory access.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError
from audit_original_cd_theme_switch import (
    ORIGINAL_SHA256, ORIGINAL_PE_TIMESTAMP, ORIGINAL_IMAGE_BASE,
    ORIGINAL_IMAGE_SIZE,
)

PAUSE = {
    'script_entry_calls_global_scan': (0x536ED,'e8 be 49 00 00'),
    'source_loop_base_and_64_entries': (0x580B0,'56 57 be b4 8c 4b 00 bf 40 00 00 00'),
    'source_field14_eligibility': (0x580BC,'8b 46 f8 85 c0 74 31'),
    'source_field08_exclusion': (0x580C3,'8b 46 ec 85 c0 75 2a'),
    'source_field1c_zero_required': (0x580CA,'83 3e 00 75 25'),
    'source_field1c_depth_stamp': (0x580CF,'a1 c8 3c 4a 00 89 06'),
    'source_field10_branch': (0x580D6,'8b 46 f4 85 c0 74 0e'),
    'source_transition_pause': (0x580E1,'e8 ba 03 00 00'),
    'source_plain_pause': (0x580EF,'e8 7c ff ff ff'),
    'source_loop_increment': (0x580F4,'83 c6 20 4f 75 c2'),
    'source_depth_increment': (0x580FA,'a1 c8 3c 4a 00 5f 40 5e a3 c8 3c 4a 00'),
    'transition_sound_object_ref': (0x584A0,'8b 4c 24 04 8b 41 04 8b 40 04'),
    'transition_routing_type1': (0x584C6,'8b 11 52 e8 d2 ce ff ff'),
    'transition_routing_type2': (0x584BD,'8b 09 51 e8 0b d3 ff ff'),
}
RESUME = {
    'script_entry_calls_resume_scan': (0x5371D,'e8 ee 49 00 00'),
    'source_depth_decrement': (0x58110,'a1 c8 3c 4a 00 56 48 57 a3 c8 3c 4a 00'),
    'source_loop_base_and_64_entries': (0x5811D,'be b4 8c 4b 00 bf 40 00 00 00'),
    'source_field14_eligibility': (0x58127,'8b 46 f8 85 c0 74 36'),
    'source_field08_exclusion': (0x5812E,'8b 46 ec 85 c0 75 2f'),
    'source_pause_depth_gate': (0x58135,'a1 c8 3c 4a 00 8b 0e 3b c8 7f 24'),
    'source_field1c_clear': (0x58143,'c7 06 00 00 00 00'),
    'source_field10_branch': (0x58140,'8b 46 f4 c7 06 00 00 00 00 85 c0 74 0e'),
    'source_transition_resume': (0x58151,'e8 1a 03 00 00'),
    'source_plain_resume': (0x5815F,'e8 2c ff ff ff'),
    'source_loop_increment': (0x58164,'83 c6 20 4f 75 bd'),
    'transition_resume_router': (0x58470,'8b 4c 24 04 8b 41 04 8b 40 04'),
}
EDGES = {
    'script_pause_to_sound_table': (0x536ED,0x580B0),
    'script_resume_to_sound_table': (0x5371D,0x58110),
    'table_transition_pause': (0x580E1,0x584A0),
    'table_plain_pause': (0x580EF,0x58070),
    'table_transition_resume': (0x58151,0x58470),
    'table_plain_resume': (0x5815F,0x58090),
    'transition_type1_pause': (0x584C9,0x553A0),
    'transition_type2_pause': (0x584C0,0x557D0),
}
SOURCE_ENTRY_COUNT=64
SOURCE_STRIDE=0x20
SOURCE_TABLE_VA=0x4B8C98

def audit(data:bytes)->dict:
    if hashlib.sha256(data).hexdigest()!=ORIGINAL_SHA256:
        raise PEFormatError('original CD SHA-256 mismatch')
    pe=PE32(data)
    if (pe.machine!=0x14C or pe.timestamp!=ORIGINAL_PE_TIMESTAMP
        or pe.image_base!=ORIGINAL_IMAGE_BASE or pe.image_size!=ORIGINAL_IMAGE_SIZE):
        raise PEFormatError('original CD PE metadata mismatch')
    for origin,sites in (('pause',PAUSE),('resume',RESUME)):
        for name,(rva,raw) in sites.items():
            expected=bytes.fromhex(raw)
            if pe.read_rva(rva,len(expected))!=expected:
                raise PEFormatError(f'original CD {origin} opcode mismatch: {name}')
    for name,(rva,target) in EDGES.items():
        raw=pe.read_rva(rva,5)
        if (raw[0]!=0xE8 or
            rva+5+int.from_bytes(raw[1:],'little',signed=True)!=target):
            raise PEFormatError(f'original CD ESC script direct edge mismatch: {name}')
    return {
        'original_cd_sha256':ORIGINAL_SHA256,
        'pause_opcode_windows_verified':len(PAUSE),
        'resume_opcode_windows_verified':len(RESUME),
        'direct_call_edges_verified':len(EDGES),
        'original_source_table_rva':'0xB8C98',
        'original_source_slots':SOURCE_ENTRY_COUNT,
        'original_source_stride_bytes':SOURCE_STRIDE,
        'source_pause_eligibility_before_dispatch_readable':True,
        'current_script_pause_callback_count_known_before_dispatch':False,
        'same_native_adpcm_generation_before_dispatch_proven':False,
        'all_entry_states_atomic_preflight_proven':False,
        'safe_to_suppress_pending_theme_pause':False,
        'native_driver_calls_or_hooks_added':False,
    }

@dataclass(frozen=True)
class Entry:
    field14:int
    field08:int
    field1c:int
    field10:int
    # Source record only, never a native ADPCM handle/generation.
    source_ref:int

def projected_pause(entries:tuple[Entry,...])->dict:
    """Momentary read-only source-table predicate, not pre-dispatch proof."""
    if len(entries)!=SOURCE_ENTRY_COUNT:
        raise ValueError('incomplete original 64-source table')
    if any(type(v) is not int or v<0 or v>0xffffffff
           for entry in entries for v in (
               entry.field14,entry.field08,entry.field1c,
               entry.field10,entry.source_ref)):
        raise ValueError('unknown or invalid source record')
    indices=[i for i,e in enumerate(entries)
             if e.field14!=0 and e.field08==0 and e.field1c==0]
    return {
        'candidate_count':len(indices),
        'transition_candidate_indices':[
            i for i in indices if entries[i].field10!=0],
        'plain_sound_candidate_indices':[
            i for i in indices if entries[i].field10==0],
        'all_source_entries_stable_until_dispatch_proven':False,
        'native_adpcm_handles_identical_proven':False,
        'native_music_generation_continuity_proven':False,
        'permission_to_skip_native_pause':False,
    }

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_original_cd_exe',type=Path)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args(argv)
    try:
        report=audit(args.owner_original_cd_exe.read_bytes())
    except (OSError,PEFormatError,ValueError) as exc:
        parser.exit(2,f'Original ESC sound-table audit refused: {exc}\n')
    if args.json:
        print(json.dumps(report,indent=2,sort_keys=True))
    else:
        print('Original 64-entry ESC source table and Pause/Resume branch paths verified')
        print('NO-GO: read-only table projection cannot certify future ADPCM stream generation')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
