#!/usr/bin/env python3
"""Fail-closed static audit of the owner-provided *original CD* Theme handoff.

Original Rayman2.exe SHA-pinned: a current Theme pointer can remain
a distinct, valid earlier resource while a pending target points to
the new outer Theme. This describes original source control flow,
NOT immutable ADPCM voice identity or permission to suppress music Pause.

No native code is executed or modified; no game bytes are distributed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

ORIGINAL_CD_SHA256 = '7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3'
ORIGINAL_SIZE = 676352
ORIGINAL_TIMESTAMP = 0x3807B68B
ORIGINAL_IMAGE_SIZE = 0x117000

# Exact original VAs converted to RVAs using the PE image base.
# These opcodes, conditional ordering, callback registration and call
# edges belong to the 1999 CD executable, NOT the modified GOG game.
ANCHORS = {
    'compare_player_ref_against_current': (0x5997B, '3b 1d cc 95 4b 00'),
    'start_pending_switch_flag_and_target': (0x595F3, '89 71 10 89 41 0c 89 71 04 89 51 44'),
    'controller_pending_target_store': (0x595FC, '89 51 44'),
    'controller_reads_pending_switch': (0x59467, '8b 41 0c 57 33 db 33 ff'),
    'controller_reads_pending_target': (0x59473, '8b 41 44'),
    'controller_clears_switch_flag': (0x59476, '89 59 0c'),
    'controller_commits_new_current': (0x5948E, '89 41 14'),
    'controller_pending_transfer_sequence': (0x59473, '8b 41 44 89 59 0c 3b c3 75 0e'),
    'callback_guard': (0x596E0, 'a1 18 96 4b 00 56 85 c0 75 67'),
    'callback_invokes_controller': (0x59707, 'e8 54 fd ff ff'),
    'theme_constructor_starts_switch': (0x599D5, 'e8 f6 fb ff ff'),
    'music_dispatch_to_theme_constructor': (0x57F38, 'e8 13 1a 00 00'),
    'register_controller_update_pointer': (0x59AE5, '68 e0 96 45 00'),
    'register_update_callback_call': (0x59AEC, 'e8 2f e7 ff ff'),
    'guarded_reader_checks_current': (0x59770, '8b 0d cc 95 4b 00'),
}
CALL_EDGES = {
    'game_audio_switch_to_theme_constructor': (0x57F38, 0x59950),
    'theme_constructor_to_pending_arming': (0x599D5, 0x595D0),
    'registered_controller_update_to_commit': (0x59707, 0x59460),
    'other_controller_update_to_commit': (0x596D3, 0x59460),
    'theme_constructor_registers_update_callback': (0x59AEC, 0x58220),
}
EXPECTED_CONTROLLER_CALLBACK_VA = 0x4596E0

class OriginalPEError(ValueError):
    pass


class PE32:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x100 or data[:2] != b'MZ':
            raise OriginalPEError('not PE: invalid DOS header')
        peoff = struct.unpack_from('<I', data, 0x3C)[0]
        if peoff < 0x40 or peoff+0xF8 > len(data) or data[peoff:peoff+4] != b'PE\0\0':
            raise OriginalPEError('not PE: invalid NT headers')
        self.machine,self.section_count,self.timestamp,_,_,optional_size,_ = struct.unpack_from(
            '<HHIIIHH',data,peoff+4)
        if optional_size < 0x60 or self.section_count < 1 or self.section_count > 16:
            raise OriginalPEError('unexpected PE section metadata')
        opt=peoff+24
        self.magic=struct.unpack_from('<H',data,opt)[0]
        self.image_base=struct.unpack_from('<I',data,opt+28)[0]
        self.image_size=struct.unpack_from('<I',data,opt+56)[0]
        self.sections=[]
        for i in range(self.section_count):
            off=opt+optional_size+i*40
            if off+40>len(data):
                raise OriginalPEError('section headers outside file')
            name,vsize,rva,rawsize,rawptr=struct.unpack_from('<8sIIII',data,off)
            if rawptr+rawsize>len(data):
                raise OriginalPEError('PE raw section exceeds file')
            self.sections.append((name.rstrip(b'\0'),rva,rawsize,rawptr))
    def read_rva(self, rva:int, length:int)->bytes:
        for name,start,size,off in self.sections:
            if (name==b'.text' and length>=0 and rva>=start
                    and rva+length<=start+size):
                return self.data[off+rva-start:off+rva-start+length]
        raise OriginalPEError(f'RVA 0x{rva:06X} outside raw original code')


def audit(data:bytes)->dict:
    sha=hashlib.sha256(data).hexdigest()
    if sha!=ORIGINAL_CD_SHA256 or len(data)!=ORIGINAL_SIZE:
        raise OriginalPEError('unrecognized original CD executable SHA-256 or size')
    pe=PE32(data)
    if (pe.machine!=0x14C or pe.magic!=0x10B
            or pe.timestamp!=ORIGINAL_TIMESTAMP
            or pe.image_base!=0x400000 or pe.image_size!=ORIGINAL_IMAGE_SIZE):
        raise OriginalPEError('CD game PE32 owner metadata mismatch')
    for label,(rva,hexcode) in ANCHORS.items():
        check=bytes.fromhex(hexcode)
        if pe.read_rva(rva,len(check))!=check:
            raise OriginalPEError(f'CD Theme handoff opcode mismatch: {label}')
    for label,(site,target) in CALL_EDGES.items():
        raw=pe.read_rva(site,5)
        if raw[0]!=0xE8 or site+5+struct.unpack_from('<i',raw,1)[0]!=target:
            raise OriginalPEError(f'CD Theme handoff call mismatch: {label}')
    at=pe.read_rva(0x59AE5,5)
    if at[0]!=0x68 or struct.unpack_from('<I',at,1)[0]!=EXPECTED_CONTROLLER_CALLBACK_VA:
        raise OriginalPEError('original controller callback registration mismatch')
    return {
        'original_cd_sha256':sha,
        'verified_opcode_windows':len(ANCHORS),
        'verified_direct_call_edges':len(CALL_EDGES),
        'pending_flag_write_va':'0x4595F6',
        'pending_target_write_va':'0x4595FC',
        'pending_arming_caller_va':'0x4599D5',
        'controller_callback_va':'0x4596E0',
        'controller_callback_registration_va':'0x459AE5',
        'controller_flag_clear_va':'0x459476',
        'controller_current_key_commit_va':'0x45948E',
        'current_key_global_va':'0x4B95CC',
        'pending_target_controller_offset':'0x44',
        'pending_flag_controller_offset':'0x0C',
        'current_theme_controller_offset':'0x14',
        'original_pending_then_current_stage_proven_statically':True,
        'pending_reference_was_ever_a_physical_music_voice_proven':False,
        'native_audio_epoch_stable_across_handoff_proven':False,
        'observed_esc_pause_during_handoff_proven':False,
        'audio_change_authorized':False,
    }


def main(argv=None)->int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('original_cd_executable',type=Path)
    p.add_argument('--json',action='store_true')
    args=p.parse_args(argv)
    try:
        result=audit(args.original_cd_executable.read_bytes())
    except (OSError,OriginalPEError,struct.error) as e:
        p.exit(2,f'Original Theme handoff audit refused: {e}\n')
    if args.json:
        print(json.dumps(result,indent=2,sort_keys=True))
    else:
        print('Original CD Theme pending->current handoff verified; NO-GO for audio bypass')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
