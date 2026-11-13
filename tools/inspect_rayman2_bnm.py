#!/usr/bin/env python3
"""Experimental read-only Rayman 2 BNM sound bank METADATA inspector.

Layout candidates: Rayman2Lib BNMFile.cs, ACP_Ray2 SND_Resource.h and
the 0x453990/0x453F90 code from the provided non-approved Rayman2 EXE.
No original retail-CD BNM has yet been verified against this parser.
Does not extract/decode/export or modify any media payload.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

HEADER_BYTES = 44
EVENT_BYTES = 32
RESOURCE_BYTES = 92
MAX_COUNT = 100_000

class BNMError(ValueError):
    pass

def word(raw: bytes, at: int, signed: bool = False) -> int:
    if at < 0 or at + 4 > len(raw):
        raise BNMError("truncated metadata")
    return struct.unpack_from("<i" if signed else "<I", raw, at)[0]

def table_bounds(raw: bytes, offset: int, count: int, stride: int,
                 label: str) -> tuple[int, int]:
    """Reject wrap/overrun and header overlap. Offsets are bank-relative."""
    if count == 0:
        return (0, 0)
    if offset < HEADER_BYTES or offset > len(raw):
        raise BNMError(f"{label} offset outside metadata area")
    if count > MAX_COUNT or count > (len(raw) - offset) // stride:
        raise BNMError(f"{label} truncated or count too large")
    return (offset, offset + count * stride)


def inspect_bytes(raw: bytes) -> dict:
    """Return candidate Theme metadata, never verified music/song identity."""
    if len(raw) < HEADER_BYTES:
        raise BNMError("BNM shorter than candidate 44-byte bank header")
    # ACP_Ray2 SND_tdstSndBankHead has independent list offsets:
    # stEventList.hFirstElementOpt at +4 and stResList at +12.
    # Native 0x453BB8/0x453BC3 relocate each original on-disk offset
    # from the bank-header base. Do not assume contiguous tables!
    (version, event_offset, event_count, resource_offset, resource_count,
     mpeg_offset, midi_offset, sample_offset, adpcm_offset, stream_offset,
     declared_total) = struct.unpack_from("<11I", raw)
    if event_count > MAX_COUNT or resource_count > MAX_COUNT:
        raise BNMError("implausible event/resource count")
    ev_begin, ev_end = table_bounds(
        raw, event_offset, event_count, EVENT_BYTES, "event table")
    res_begin, res_end = table_bounds(
        raw, resource_offset, resource_count, RESOURCE_BYTES, "resource table")
    if event_count and resource_count and max(ev_begin,res_begin)<min(ev_end,res_end):
        raise BNMError("event and resource metadata tables overlap")
    declared_media={
        "MPEG":mpeg_offset, "MIDI":midi_offset, "Sample":sample_offset,
        "ADPCM":adpcm_offset, "Streaming":stream_offset,
    }
    warnings = []
    if declared_total and declared_total != len(raw):
        warnings.append("declared total differs from physical file size")
    if any(value > len(raw) for value in declared_media.values()):
        warnings.append("one or more declared media offsets beyond file; origin unverified")

    types = {}
    resource_ids=[]
    themes = []
    for i in range(resource_count):
        p = res_begin + i * RESOURCE_BYTES
        ident = word(raw, p)
        kind = word(raw, p + 4)
        storage = word(raw, p + 8)
        types[str(kind)] = types.get(str(kind), 0) + 1
        resource_ids.append((ident,kind))
        if kind == 11:  # SND_E_TR_Theme, not SND_tdeTypeEvent
            themes.append({
                "table_index": i,
                "packed_id_hex": f"0x{ident:08X}",
                "bank_group_from_id": ident >> 16,
                "record_index_from_id": ident & 0xFFFF,
                "storage_kind": storage,
                # SND_tdstBlockResourceDisk has declared length & offset.
                # These are metadata, never verified stream lengths.
                "disk_data_size_declared_unverified":word(raw,p+12),
                "disk_data_offset_declared_unverified":word(raw,p+16),
                "volume_byte_candidate": raw[p + 20],
                "number_of_parts_candidate": word(raw, p + 36),
                "outro_offset_candidate": word(raw, p + 40, signed=True),
                "layout_not_confirmed_on_retail_cd": True,
            })
    # ACP_Ray2 SND_tdstBlockEvent: Id +0, eType +4;
    # SND_EVT_Son_Play=1 has uParams.stPlay.uResource.Id at +8.
    # Native 0x453DC9/0x453DD6 resolves that exact field.
    event_types={}
    play_events=[]
    local_theme_refs=[]
    for i in range(event_count):
        p=ev_begin+i*EVENT_BYTES
        event_id=word(raw,p)
        event_kind=word(raw,p+4)
        event_types[str(event_kind)]=event_types.get(str(event_kind),0)+1
        if event_kind!=1:
            continue
        ref=word(raw,p+8)
        if ref==0xFFFFFFFF:
            status="null-resource-reference"
            target_index=None
        else:
            # Native 0x453F90: upper16 looks up the bank, lower16
            # DIRECTLY indexes its 92-byte resource table. Do not
            # search resource.Id fields by value: duplicate or
            # out-of-order records could yield a false Theme match.
            index=ref & 0xFFFF
            if index>=len(resource_ids):
                status="not-in-this-bank"
                target_index=None
            elif resource_ids[index][0]!=ref:
                status="bank-id-or-record-index-mismatch"
                target_index=None
            else:
                target_index=index
                status=("local-theme" if resource_ids[index][1]==11
                        else "local-non-theme")
        event={
            "event_table_index":i,
            "event_id_hex":f"0x{event_id:08X}",
            "resource_id_hex":f"0x{ref:08X}",
            "candidate_resolution":status,
            "local_resource_table_index":target_index,
        }
        play_events.append(event)
        if status=="local-theme":
            local_theme_refs.append(event)

    return {
        "candidate_format": "CPA Rayman 2 BNM metadata",
        "physical_size": len(raw),
        "header_version_candidate": version,
        "events_declared": event_count,
        "resources_declared": resource_count,
        "record_sizes": {"header": 44, "event": 32, "resource": 92},
        "table_ranges_file_relative":{
            "events":[ev_begin,ev_end],
            "resources":[res_begin,res_end],
        },
        "candidate_metadata_end": max(HEADER_BYTES,ev_end,res_end),
        "media_offsets_unverified":declared_media,
        "declared_total_unverified": declared_total,
        "resource_type_counts": types,
        "event_type_counts":event_types,
        "theme_resources_candidate": themes,
        "play_event_references_candidate":play_events,
        "local_play_events_referring_to_themes_candidate":local_theme_refs,
        "warnings": warnings,
        "verified_against_actual_retail_cd_bnm": False,
        "audio_extracted": False,
    }

def inspect_file(path: Path) -> dict:
    raw = path.read_bytes()
    result = inspect_bytes(raw)
    result["source_file"] = path.name
    result["sha256"] = hashlib.sha256(raw).hexdigest()
    return result

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bank", type=Path, nargs="+", help="user-owned BNM files")
    results = []
    for bank in parser.parse_args().bank:
        try:
            results.append(inspect_file(bank))
        except (BNMError, OSError) as error:
            results.append({"source_file": bank.name, "error": str(error)})
    print(json.dumps(results, indent=2, ensure_ascii=False))

if __name__ == "__main__":
    main()
