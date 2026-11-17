#!/usr/bin/env python3
"""Original paired WAVMW/APMMX type-4 Pause/Resume: exact, read-only audit.

The two original public type-4 routes each dispatch one driver delegate.
On a VALID, currently bound APMMX record, Pause sets [+0x1C] = 1 and
Resume sets [+0x1C] = 0; neither checks a prior pause state before writing.
This supports researching a future immediate-only bypass, NOT enabling it.
These are exact-original opcode facts, not immutable voice generation.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
from pathlib import Path
from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256
from audit_adpcm_lock_routes import WAVMW_OWNER_SHA256

# Verified from exact original DLL disassemblies. All entries are RVA,
# no absolute process pointers and no native audio execution.
WAV_SITES = {
    'pause_type4_compare': (0xC7EC,'83 e8 02'),
    'pause_type4_branch': (0xC7EF,'74 0f'),
    'pause_type4_dispatch': (0xC800,'8b 41 04 50 e8 57 74 ff ff'),
    'pause_trampoline_pointer': (0x3C60,'a1 9c 6a 02 10'),
    'pause_trampoline_call': (0x3C74,'ff 15 9c 6a 02 10'),
    'resume_type4_compare': (0xC83C,'83 e8 02'),
    'resume_type4_branch': (0xC83F,'74 0f'),
    'resume_type4_dispatch': (0xC850,'8b 41 04 50 e8 37 74 ff ff'),
    'resume_trampoline_pointer': (0x3C90,'a1 a0 6a 02 10'),
    'resume_trampoline_call': (0x3CA4,'ff 15 a0 6a 02 10'),
}
WAV_CALLS = {
    'pause_type4_to_trampoline': (0xC804,0x3C60),
    'resume_type4_to_trampoline': (0xC854,0x3C90),
}
WAV_BRANCHES = {
    'pause_type4_selection': (0xC7EF,0xC800),
    'resume_type4_selection': (0xC83F,0xC850),
}
APM_SITES = {
    'pause_export': (0x54D0,'8b 44 24 04 50 e8 a6 de ff ff 83 c4 04 c2 04 00'),
    'resume_export': (0x54E0,'8b 44 24 04 50 e8 d6 de ff ff 83 c4 04 c2 04 00'),
    'pause_wrapper_initialized': (0x3380,'a1 a8 82 0e 10 85 c0 74 27'),
    'pause_wrapper_lock': (0x338E,'e8 8d dc ff ff'),
    'pause_wrapper_apply': (0x339B,'e8 30 eb ff ff'),
    'pause_wrapper_unlock': (0x33A8,'e8 83 dc ff ff'),
    'resume_wrapper_initialized': (0x33C0,'a1 a8 82 0e 10 85 c0 74 27'),
    'resume_wrapper_lock': (0x33CE,'e8 4d dc ff ff'),
    'resume_wrapper_apply': (0x33DB,'e8 30 eb ff ff'),
    'resume_wrapper_unlock': (0x33E8,'e8 43 dc ff ff'),
    'pause_validate_handle': (0x1EDD,'e8 9e 05 00 00'),
    'pause_invalid_return': (0x1EE9,'b8 0e 00 00 00'),
    'pause_esc_null_return': (0x1EFB,'b8 0b 00 00 00'),
    'pause_write_one': (0x1EF0,'c7 46 1c 01 00 00 00'),
    'resume_validate_handle': (0x1F1D,'e8 5e 05 00 00'),
    'resume_invalid_return': (0x1F29,'b8 0e 00 00 00'),
    'resume_esc_null_return': (0x1F3B,'b8 0b 00 00 00'),
    'resume_write_zero': (0x1F30,'c7 46 1c 00 00 00 00'),
}
APM_CALLS = {
    'pause_export_to_wrapper': (0x54D5,0x3380),
    'resume_export_to_wrapper': (0x54E5,0x33C0),
    'pause_wrapper_to_acquire': (0x338E,0x1020),
    'pause_wrapper_to_apply': (0x339B,0x1ED0),
    'pause_wrapper_to_release': (0x33A8,0x1030),
    'resume_wrapper_to_acquire': (0x33CE,0x1020),
    'resume_wrapper_to_apply': (0x33DB,0x1F10),
    'resume_wrapper_to_release': (0x33E8,0x1030),
    'pause_apply_to_validator': (0x1EDD,0x2480),
    'resume_apply_to_validator': (0x1F1D,0x2480),
}

def _metadata(pe:PE32, module:str)->bool:
    if module=='wav':
        return (pe.machine==0x14C and pe.timestamp==0x38076FB4
                and pe.image_base==0x10000000 and pe.image_size==0x87000)
    return (pe.machine==0x14C and pe.timestamp==0x38076F98
            and pe.image_base==0x10000000 and pe.image_size==0xEE000)

def _validate(data:bytes, expected_hash:str, module:str, sites:dict, calls:dict)->PE32:
    if hashlib.sha256(data).hexdigest()!=expected_hash:
        raise PEFormatError(f'{module} original SHA-256 mismatch')
    pe=PE32(data)
    if not _metadata(pe,module):
        raise PEFormatError(f'{module} original PE metadata mismatch')
    for name,(rva,encoded) in sites.items():
        b=bytes.fromhex(encoded)
        if pe.read_rva(rva,len(b))!=b:
            raise PEFormatError(f'{module} original opcode changed: {name}')
    for name,(site,target) in calls.items():
        b=pe.read_rva(site,5)
        if b[0]!=0xE8 or site+5+int.from_bytes(b[1:],'little',signed=True)!=target:
            raise PEFormatError(f'{module} original direct call changed: {name}')
    return pe

def audit(wav_bytes:bytes, apm_bytes:bytes)->dict:
    wav=_validate(wav_bytes,WAVMW_OWNER_SHA256,'wav',WAV_SITES,WAV_CALLS)
    apm=_validate(apm_bytes,OWNER_ORIGINAL_SHA256,'apm',APM_SITES,APM_CALLS)
    exw=wav.exports()
    exa=apm.exports()
    for name,rva in (
        ('_SND_fn_vPauseTransitionSxd@4',0xC7E0),
        ('_SND_fn_vResumeTransitionSxd@4',0xC830),
    ):
        if exw.get(name)!=rva:
            raise PEFormatError('wav original type-4 transition export changed: '+name)
    for name,rva in (
        ('_SND_fn_vPauseTransitionSxdADPCM@4',0x54D0),
        ('_SND_fn_vResumeTransitionSxdADPCM@4',0x54E0),
    ):
        if exa.get(name)!=rva:
            raise PEFormatError('apm original ADPCM transition export changed: '+name)
    for name,(site,target) in WAV_BRANCHES.items():
        b=wav.read_rva(site,2)
        if b[0]!=0x74 or site+2+int.from_bytes(b[1:],'little',signed=True)!=target:
            raise PEFormatError('wav original type-4 branch changed: '+name)
    return {
        'original_wavmw_sha256':WAVMW_OWNER_SHA256,
        'original_apmmx_sha256':OWNER_ORIGINAL_SHA256,
        'wav_opcode_windows_verified':len(WAV_SITES),
        'wav_direct_edges_verified':len(WAV_CALLS),
        'wav_type4_branches_verified':len(WAV_BRANCHES),
        'apm_opcode_windows_verified':len(APM_SITES),
        'apm_direct_edges_verified':len(APM_CALLS),
        'wav_type4_pause_calls_one_dynamic_delegate':True,
        'wav_type4_resume_calls_one_dynamic_delegate':True,
        'apm_valid_record_pause_writes_one':True,
        'apm_valid_record_resume_writes_zero_without_reading_previous_pause_flag':True,
        'native_wrapper_calls_known_driver_lock_and_unlock':True,
        'can_skip_two_early_pause_calls_atomically_proven':False,
        'live_music_voice_generation_proven':False,
        'resume_on_reused_record_invariant_proven':False,
        'safe_to_enable_early_esc_pause_suppression':False,
        'no_native_execution_or_hooking':True,
        'interpretation':('Same-current-valid-record pause bit is deterministic. '
                          'Before source Pauses execute, their complete pair and '
                          'record generation are not yet proven; no deferred '
                          'Resume or early Pause interception is authorized.'),
    }


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('original_wavmw',type=Path)
    p.add_argument('original_apmmx',type=Path)
    p.add_argument('--json',action='store_true')
    args=p.parse_args(argv)
    try:
        result=audit(args.original_wavmw.read_bytes(),args.original_apmmx.read_bytes())
    except (OSError,PEFormatError,OverflowError) as exc:
        p.exit(2,f'Original type-4 pause/resume audit refused: {exc}\n')
    if args.json:
        print(json.dumps(result,sort_keys=True,indent=2))
    else:
        print('Exact-original WAVMW type-4 Pause/Resume and APMMX lock+flag writes verified')
        print('NO-GO: do not intercept pre-driver Pause or add delayed native Resume')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
