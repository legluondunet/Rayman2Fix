#!/usr/bin/env python3
"""Read-only, hash-locked static evidence for Rayman 2 CD Theme switch.

The owner's original, SHA-approved Rayman2(3).exe is a PE32 x86 game
binary. No game code is executed, hooked, patched, loaded, or copied.
Only the exact original SHA-256 authorizes interpreting the instruction
bytes and controller fields listed in this audit. Uses stdlib + the
existing PE32 parser in tools/audit_apmmxbvr.py.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError

ORIGINAL_SHA256 = "7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3"
ORIGINAL_PE_TIMESTAMP = 0x3807B68B
ORIGINAL_IMAGE_BASE = 0x400000
ORIGINAL_IMAGE_SIZE = 0x117000

# 1999 original PE entry address minus image base. These are verified
# instruction-byte fingerprints, NOT a general-purpose x86 decoder.
# Tag 'write' and 'read' refer to the local instruction operands,
# not an atomic global snapshot or a driver/music stream identity.
SITES = (
    ("initial_current", 0x5967B, "89 48 14", "write", "+0x14 = incoming Theme"),
    ("initial_pending_clear", 0x5967E, "89 50 0c", "write", "+0x0C = 0"),
    ("explicit_pending_flag", 0x595F6, "89 41 0c", "write", "+0x0C = 1"),
    ("explicit_pending_target", 0x595FC, "89 51 44", "write", "+0x44 = requested Theme"),
    ("progress_checks_pending", 0x59467, "8b 41 0c", "read", "check +0x0C"),
    ("progress_reads_target", 0x59473, "8b 41 44", "read", "read +0x44"),
    ("progress_clears_pending", 0x59476, "89 59 0c", "write", "+0x0C = 0"),
    ("progress_promotes_target", 0x5948E, "89 41 14", "write", "+0x14 = pending Theme"),
    ("end_of_part_requeues_flag", 0x59529, "89 41 0c", "write", "+0x0C = 1 again"),
    ("end_of_part_reads_next", 0x59534, "8b 15 9c 94 4b 00", "read", "load next Theme from global 0x4B949C"),
    ("end_of_part_rewrites_target", 0x5953A, "89 51 44", "write", "+0x44 = next Theme"),
    ("player_progress_calls_update", 0x59707, "e8 54 fd ff ff", "call", "call original 0x459460 updater"),
    ("native_query_pending", 0x597C2, "a1 c4 95 4b 00", "read", "query +0x0C"),
    ("theme_update_reads_pending", 0x5996E, "8b 0d c4 95 4b 00", "read", "read +0x0C"),
    ("theme_update_compares_current", 0x5997B, "3b 1d cc 95 4b 00", "read", "compare current +0x14"),
)


def audit(data: bytes, expected_sha256: str = ORIGINAL_SHA256) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected_sha256.lower():
        raise PEFormatError(
            "retail CD SHA-256 does not match: refusing all controller RVA analysis"
        )
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != ORIGINAL_PE_TIMESTAMP or
            pe.image_base != ORIGINAL_IMAGE_BASE or
            pe.image_size != ORIGINAL_IMAGE_SIZE):
        raise PEFormatError("SHA-approved CD PE header fingerprint mismatch")
    results = []
    for name, rva, hex_bytes, action, explanation in SITES:
        required = bytes.fromhex(hex_bytes)
        actual = pe.read_rva(rva, len(required))
        if actual != required:
            raise PEFormatError(
                f"SHA-approved CD opcode mismatch at {name}, RVA 0x{rva:05x}"
            )
        results.append({
            "site": name,
            "rva": f"0x{rva:05X}",
            "original_va": f"0x{ORIGINAL_IMAGE_BASE+rva:08X}",
            "opcode_hex": required.hex(" ").upper(),
            "access": action,
            "static_observation": explanation,
        })
    return {
        "sha256": digest,
        "bytes": len(data),
        "image_base": f"0x{pe.image_base:08X}",
        "timestamp": f"0x{pe.timestamp:08X}",
        "site_count": len(results),
        "sites": results,
        "observations": [
            "The native player queues an incoming Theme at controller+0x44 while setting +0x0C=1.",
            "The progress/update path clears +0x0C BEFORE committing +0x44 to current +0x14.",
            "A separate original progress branch can REWRITE +0x44 with the next Theme and set +0x0C again.",
            "No transient controller snapshot, pending target or same ADPCM pool ordinal proves a persistent physical sound owner.",
        ],
        "action_policy": "READ_ONLY_NO_NATIVE_AUDIO_INTERVENTION",
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("approved_retail_cd_exe", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    try:
        results = audit(args.approved_retail_cd_exe.read_bytes())
    except (OSError, ValueError, OverflowError) as exc:
        print(f"CD Theme audit: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print(f"Original SHA256: {results['sha256']}")
        print(f"Original verified opcode locations: {results['site_count']}")
        for site in results["sites"]:
            print(f"  {site['original_va']}: {site['site']}: "
                  f"{site['static_observation']} [{site['access']}]")
        for observation in results["observations"]:
            print(f"  NOTE: {observation}")
        print(results["action_policy"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
