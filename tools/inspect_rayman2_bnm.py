#!/usr/bin/env python3
"""Experimental read-only Rayman 2 BNM sound bank METADATA inspector.

Layout candidates: Rayman2Lib BNMFile.cs, ACP_Ray2 SND_Resource.h and
the 0x453990/0x453F90 code from the provided non-approved Rayman2 EXE.
No original retail-CD BNM has yet been verified against this parser.
Does not read/extract/decode/export any media payload.
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

def inspect_bytes(raw: bytes) -> dict:
    """Return candidate Theme metadata, never verified music/song identity."""
    if len(raw) < HEADER_BYTES:
        raise BNMError("BNM shorter than candidate 44-byte bank header")
    version, _, event_count, _, resource_count, *other = (
        struct.unpack_from("<11I", raw))
    if event_count > MAX_COUNT or resource_count > MAX_COUNT:
        raise BNMError("implausible event/resource count")
    first_audio_offset = other[0]
    declared_total = other[-1]
    start = HEADER_BYTES + EVENT_BYTES * event_count
    end = start + RESOURCE_BYTES * resource_count
    if end > len(raw):
        raise BNMError("truncated candidate event/resource metadata")

    warnings = []
    if first_audio_offset and first_audio_offset < end:
        warnings.append("first audio offset overlaps candidate records")
    if first_audio_offset > len(raw):
        warnings.append("first audio offset beyond physical file")
    if declared_total and declared_total != len(raw):
        warnings.append("declared total differs from physical file size")

    types = {}
    themes = []
    for i in range(resource_count):
        p = start + i * RESOURCE_BYTES
        ident = word(raw, p)
        kind = word(raw, p + 4)
        storage = word(raw, p + 8)
        types[str(kind)] = types.get(str(kind), 0) + 1
        if kind == 11:  # SND_E_TR_Theme, not SND_tdeTypeEvent
            themes.append({
                "table_index": i,
                "packed_id_hex": f"0x{ident:08X}",
                "bank_group_from_id": ident >> 16,
                "record_index_from_id": ident & 0xFFFF,
                "storage_kind": storage,
                "volume_byte_candidate": raw[p + 20],
                "number_of_parts_candidate": word(raw, p + 36),
                "outro_offset_candidate": word(raw, p + 40, signed=True),
                "layout_not_confirmed_on_retail_cd": True,
            })
    return {
        "candidate_format": "CPA Rayman 2 BNM metadata",
        "physical_size": len(raw),
        "header_version_candidate": version,
        "events_declared": event_count,
        "resources_declared": resource_count,
        "record_sizes": {"header": 44, "event": 32, "resource": 92},
        "candidate_metadata_end": end,
        "declared_audio_start_unverified": first_audio_offset,
        "declared_total_unverified": declared_total,
        "resource_type_counts": types,
        "theme_resources_candidate": themes,
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
