#!/usr/bin/env python3
"""SHA-locked, read-only original CD dual-dispatch Pause/Resume route audit.

Separates one direct transition-source reference from an *additional*
generic Theme-11 source that delegates to its inner transition. Both
can reach the SAME native WAVMW callback; whether they do at a
particular ESC remains a runtime question. No proprietary bytes here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError
from audit_original_cd_theme_switch import (
    ORIGINAL_SHA256, ORIGINAL_PE_TIMESTAMP,
    ORIGINAL_IMAGE_BASE, ORIGINAL_IMAGE_SIZE,
)

# Original PE image RVAs, never image-independent external offsets.
ANCHORS = {
    'pause_source_selects_direct_or_generic': (0x580D6,'8b 46 f4 85 c0 74 0e'),
    'pause_source_direct_call': (0x580E1,'e8 ba 03 00 00'),
    'pause_source_generic_call': (0x580EF,'e8 7c ff ff ff'),
    'resume_source_direct_call': (0x58151,'e8 1a 03 00 00'),
    'resume_source_generic_call': (0x5815F,'e8 2c ff ff ff'),
    'generic_pause_tag_deref': (0x58070,'8b 4c 24 04 8b 41 04 8b 40 04 8d 14 40 8b 01 50'),
    'generic_pause_vtable_dispatch': (0x58080,'ff 14 d5 a8 3b 4a 00'),
    'generic_resume_tag_deref': (0x58090,'8b 4c 24 04 8b 41 04 8b 40 04 8d 14 40 8b 01 50'),
    'generic_resume_vtable_dispatch': (0x580A0,'ff 14 d5 ac 3b 4a 00'),
    'theme11_pause_active_and_inner': (0x59C40,'8b 44 24 04 83 38 00 74 0c 8b 40 30 50'),
    'theme11_resume_active_and_inner': (0x59C60,'8b 44 24 04 83 38 00 74 0c 8b 40 30 50'),
    'type1_direct_pause_to_callback': (0x584C6,'8b 11 52 e8 d2 ce ff ff'),
    'type1_direct_resume_to_callback': (0x58496,'8b 11 52 e8 22 cf ff ff'),
    'pause_callback_indirect': (0x553B4,'ff 15 08 87 4b 00'),
    'resume_callback_indirect': (0x553D4,'ff 15 0c 87 4b 00'),
}
EDGES = {
    'source_direct_pause':(0x580E1,0x584A0),
    'source_generic_pause':(0x580EF,0x58070),
    'source_direct_resume':(0x58151,0x58470),
    'source_generic_resume':(0x5815F,0x58090),
    'theme11_to_inner_pause':(0x59C4D,0x584A0),
    'theme11_to_inner_resume':(0x59C6D,0x58470),
    'inner_type1_to_callback_pause':(0x584C9,0x553A0),
    'inner_type1_to_callback_resume':(0x58499,0x553C0),
}
GENERIC_PAUSE_TABLE_RVA=0xA3BA8
GENERIC_RESUME_TABLE_RVA=0xA3BAC
TYPE_STRIDE=24
THEME_TYPE=11
EXPECTED_PAUSE_ROUTE_RVA=0x59C40
EXPECTED_RESUME_ROUTE_RVA=0x59C60

def audit(data:bytes)->dict:
    if hashlib.sha256(data).hexdigest()!=ORIGINAL_SHA256:
        raise PEFormatError('original CD SHA-256 mismatch')
    pe=PE32(data)
    if (pe.machine!=0x14c or pe.timestamp!=ORIGINAL_PE_TIMESTAMP or
        pe.image_base!=ORIGINAL_IMAGE_BASE or
        pe.image_size!=ORIGINAL_IMAGE_SIZE):
        raise PEFormatError('original CD PE metadata mismatch')
    for name,(rva,raw) in ANCHORS.items():
        expected=bytes.fromhex(raw)
        if pe.read_rva(rva,len(expected))!=expected:
            raise PEFormatError('dual-dispatch opcode mismatch: '+name)
    for name,(at,target) in EDGES.items():
        code=pe.read_rva(at,5)
        if code[0]!=0xe8 or at+5+int.from_bytes(code[1:],
                                                    'little',signed=True)!=target:
            raise PEFormatError('dual-dispatch call edge mismatch: '+name)
    for name,table_rva,expected in (
        ('pause',GENERIC_PAUSE_TABLE_RVA,EXPECTED_PAUSE_ROUTE_RVA),
        ('resume',GENERIC_RESUME_TABLE_RVA,EXPECTED_RESUME_ROUTE_RVA),
    ):
        actual=int.from_bytes(pe.read_rva(table_rva+THEME_TYPE*TYPE_STRIDE,4),
                              'little')
        if actual!=pe.image_base+expected:
            raise PEFormatError('original Theme-11 '+name+' vtable mismatch')
    return {
        'original_cd_sha256':ORIGINAL_SHA256,
        'opcode_windows_verified':len(ANCHORS),
        'direct_call_edges_verified':len(EDGES),
        'theme_11_pause_and_resume_vtable_pointers_verified':True,
        'direct_source_transition_pause_path_verified':True,
        'generic_theme_11_pause_to_nested_transition_path_verified':True,
        'same_native_callback_target_possible':True,
        'one_direct_alias_and_two_callback_events_compatible':True,
        'two_source_entries_actually_active_at_one_esc_proven':False,
        'same_physical_voice_generation_proven':False,
        'safe_to_suppress_native_pause':False,
        'native_code_or_calls_modified':False,
    }

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_original_cd_exe',type=Path)
    parser.add_argument('--json',action='store_true')
    args=parser.parse_args(argv)
    try:
        report=audit(args.owner_original_cd_exe.read_bytes())
    except (OSError,PEFormatError,ValueError) as exc:
        parser.exit(2,f'Original dual-dispatch route audit refused: {exc}\n')
    if args.json:print(json.dumps(report,indent=2,sort_keys=True))
    else:
        print('Original dual-dispatch Theme-11 + direct SXD call paths verified')
        print('NO-GO: path reachability is not runtime source or voice-epoch proof')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
