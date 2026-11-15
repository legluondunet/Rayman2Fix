#!/usr/bin/env python3
"""Read-only call-site audit for the owner's hash-pinned 1999 APMMXbvr.dll.

Reports raw E8+rel32 candidate call sites targeting the original
record-retirement, allocation, GetPos and TestIsPlaying helper chains.
The targets are also independently disassembled against the exact
owner DLL. This is NOT a complete x86 call graph: indirect calls and non-E8 control flow
are unobservable, and an E8 byte inside instruction data is only a
candidate until independently disassembled.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256

EXPECTED = {
    # Original primary voice allocator; both play entry routes.
    0x13A0: (0x27BE, 0x3223),
    # Original retirement: exported Stop and internal cleanup routes.
    0x15D0: (0x172C, 0x1C8D, 0x1F68, 0x2950),
    # Shared record initializer: primary and *auxiliary* records.
    0x1FC0: (0x14F9, 0x21F3, 0x22A3),
    # Parameter updates can retire *inactive* records without an
    # exported Stop. Both callers use original driver locking.
    0x1C30: (0x28D3, 0x3278),
    # Exports SetParamSxd@8 and SetParamTransitionSxd@8.
    0x28B0: (0x513C,),
    0x3250: (0x544C,),
    # Internal inactive record cleanup, reached directly by TestIsPlaying.
    0x1F50: (0x290E,),
    # Internal position/progression reader, reached directly by GetPos.
    0x2320: (0x2A20,),
    # Original handle validation helper (many different original paths).
    0x2480: (0x15EB, 0x170E, 0x1C3F, 0x1EDD, 0x1F1D,
             0x1F8D, 0x218E, 0x2222, 0x22DE, 0x2355),
    # Public APMMX TestIsPlaying and GetPos entry stubs.
    0x2900: (0x5155,),
    0x2A00: (0x51A5,),
}


def candidates(text: bytes, base_rva: int, target_rva: int) -> tuple[int, ...]:
    """Enumerate every raw direct E8 rel32 target, without decoding instructions."""
    found = []
    for pos in range(max(0, len(text)-4)):
        if text[pos] != 0xE8:
            continue
        disp = int.from_bytes(text[pos+1:pos+5], "little", signed=True)
        if base_rva + pos + 5 + disp == target_rva:
            found.append(base_rva+pos)
    return tuple(found)


def audit(data: bytes) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if digest != OWNER_ORIGINAL_SHA256:
        raise PEFormatError("original APMMX SHA-256 mismatch")
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076F98
            or pe.image_base != 0x10000000 or pe.image_size != 0xEE000):
        raise PEFormatError("original APMMX PE metadata mismatch")
    text_sections = [s for s in pe.sections if s[0] == ".text"]
    if len(text_sections) != 1:
        raise PEFormatError("original APMMX has no unique .text")
    name, rva, virtual_size, raw_offset, raw_size = text_sections[0]
    if rva != 0x1000 or raw_size != 0xD800 or virtual_size <= 0:
        raise PEFormatError("original APMMX .text layout mismatch")
    raw = pe.read_rva(rva, raw_size)
    verified = {}
    for target, expected in EXPECTED.items():
        observed = candidates(raw, rva, target)
        if observed != expected:
            raise PEFormatError(
                f"direct rel32 call candidates to RVA 0x{target:X} "
                f"differ from verified expected sites"
            )
        verified[f"0x{target:04X}"] = [f"0x{site:04X}" for site in observed]
    return {
        "original_sha256": digest,
        "raw_text_bytes": len(raw),
        "direct_rel32_candidates": verified,
        "raw_direct_call_candidates_verified": True,
        "direct_target_count": len(EXPECTED),
        "direct_callsite_count": sum(len(sites) for sites in verified.values()),
        "query_internal_helpers_have_one_direct_candidate_each": (
            verified["0x1F50"] == ["0x290E"] and
            verified["0x2320"] == ["0x2A20"]
        ),
        "setparam_hidden_retire_rva": "0x1C8D",
        "setparam_entry_routes_verified": (
            verified["0x1C30"] == ["0x28D3", "0x3278"] and
            verified["0x28B0"] == ["0x513C"] and
            verified["0x3250"] == ["0x544C"]
        ),
        "unique_original_query_runtime_invocation_proven": False,
        "indirect_or_dynamic_call_paths_exhausted": False,
        "native_generation_proven": False,
        "permission_to_modify_audio": False,
        "limit": ("Only raw E8+rel32 candidates within original .text. "
                  "Not a complete x86 control-flow graph; indirect calls, "
                  "alternate entry points, concurrent lifetimes, and "
                  "physical stream generation remain unproven."),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = audit(args.dll.read_bytes())
    except (OSError, PEFormatError, OverflowError) as exc:
        print(f"APMMX direct-call audit refused: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for target, sites in result["direct_rel32_candidates"].items():
            print(f"RVA {target} direct rel32 candidates: {', '.join(sites)}")
        print(result["limit"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
