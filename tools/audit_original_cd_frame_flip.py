#!/usr/bin/env python3
"""SHA-locked retail-CD original GLI frame-flip fallback research.

Offline only: verifies that the known game GLI frame-flip wrapper
forwards the existing 1999 driver and that 4 original source callsites
reach this wrapper, including movie-frame paths. Does not prove
runtime callback cadence or assert that every scene invokes it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError
from audit_original_cd_theme_switch import (
    ORIGINAL_SHA256, ORIGINAL_PE_TIMESTAMP,
    ORIGINAL_IMAGE_BASE, ORIGINAL_IMAGE_SIZE
)

FRAME_FLIP_ENTRY_RVA = 0x20F50
ORIGINAL_ENTRY_BYTES = bytes.fromhex('8b 0d 08 72 4a 00')
DRIVER_FLIP_CALL_RVA = 0x20F72
DRIVER_FLIP_SLOT_VA = 0x504538
NATIVE_FLIP_CALLERS = (0x0106C, 0x0AE81, 0x5EEA3, 0x5EF87)
# 0x421C9C resolves "GLI_DRV_vFlipDeviceWithSyncro"; original
# loader publishes the callback pointer to 0x504538 at 0x421CB5.
ROUTE_INSTRUCTIONS = {
    'frame_flip_start': (0x20F50,'8b 0d 08 72 4a 00'),
    'native_driver_sync_flip': (0x20F72,'ff 15 38 45 50 00'),
    'frame_flip_returns_one': (0x20F9D,'b8 01 00 00 00 c3'),
    'resolve_sync_flip_name': (0x21C9C,'68 8c ff 49 00'),
    'sync_flip_driver_slot_published': (0x21CB5,'a3 38 45 50 00'),
    'cinematic_flip_caller_one': (0x5EEA3,'e8 a8 20 fc ff'),
    'cinematic_flip_caller_two': (0x5EF87,'e8 c4 1f fc ff'),
    'game_flip_caller': (0x0AE81,'e8 ca 60 01 00'),
}
SYNC_FLIP_SYMBOL = b'GLI_DRV_vFlipDeviceWithSyncro\0'
SYNC_FLIP_SYMBOL_RVA = 0x9FF8C


def audit(raw: bytes) -> dict:
    if hashlib.sha256(raw).hexdigest()!=ORIGINAL_SHA256:
        raise PEFormatError('original retail CD SHA-256 mismatch')
    pe=PE32(raw)
    if (pe.machine!=0x14c or pe.timestamp!=ORIGINAL_PE_TIMESTAMP or
        pe.image_base!=ORIGINAL_IMAGE_BASE or
        pe.image_size!=ORIGINAL_IMAGE_SIZE):
        raise PEFormatError('original retail CD PE metadata mismatch')
    for key,(at,expected_hex) in ROUTE_INSTRUCTIONS.items():
        expected=bytes.fromhex(expected_hex)
        if pe.read_rva(at,len(expected))!=expected:
            raise PEFormatError('original GLI flip opcode mismatch: '+key)
    if pe.read_rva(SYNC_FLIP_SYMBOL_RVA,len(SYNC_FLIP_SYMBOL))!=SYNC_FLIP_SYMBOL:
        raise PEFormatError('original sync-flip resolver symbol mismatch')
    for at in NATIVE_FLIP_CALLERS:
        op=pe.read_rva(at,5)
        target=at+5+int.from_bytes(op[1:],'little',signed=True)
        if op[0]!=0xE8 or target!=FRAME_FLIP_ENTRY_RVA:
            raise PEFormatError(f'original frame-flip caller mismatch: {at:#x}')
    return {
        'original_cd_sha256':ORIGINAL_SHA256,
        'frame_flip_wrapper_rva':f'{FRAME_FLIP_ENTRY_RVA:#x}',
        'game_sync_driver_slot_va':f'{DRIVER_FLIP_SLOT_VA:#x}',
        'opcode_sites_verified':len(ROUTE_INSTRUCTIONS),
        'verified_original_callers':len(NATIVE_FLIP_CALLERS),
        'original_driver_is_forwarded':True,
        'source_callsites_include_cinematic_rendering':True,
        'live_frame_cadence_during_jail_20_proven':False,
        'runtime_thread_identity_proven':False,
        'physical_audio_changes_authorized':False,
        'original_game_code_modified':False
    }


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('owner_retail_cd_executable',type=Path)
    parser.add_argument('--json',action='store_true')
    options=parser.parse_args(argv)
    try:
        result=audit(options.owner_retail_cd_executable.read_bytes())
    except (OSError,PEFormatError,ValueError) as exc:
        parser.exit(2,f'Retail CD flip audit refused: {exc}\n')
    if options.json: print(json.dumps(result,indent=2,sort_keys=True))
    else:
        print('Approved original GLI frame-flip and four game callsites verified')
        print('No claim about live cinematic cadence without an owner test')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
