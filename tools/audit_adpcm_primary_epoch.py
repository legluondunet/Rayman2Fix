#!/usr/bin/env python3
"""Read-only, hash-pinned research of original APMMX primary voice creation.

The original initializer is shared with auxiliary records. Distinguish
successfully returned *primary handle publication* from record setup.
This cannot prove a physical song epoch, authorize runtime hooks, or
provide exhaustive synchronization/indirect-call coverage.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256

# Original RVA: exact opcode anchors from independently disassembled
# owner-supplied 1999 PE32 DLL; no copyrighted binary bytes are bundled.
SITES = {
    "primary_allocator_entry": (0x13A0, "a1 c4 4e 02 10"),
    "active_record_eligibility": (0x1460, "8b 56 28 85 d2 75 28"),
    "in_place_handle_reuse": (0x14BE, "8b ae d4 00 00 00"),
    "first_free_handle": (0x14A1, "8b 0d 48 4e 02 10"),
    "publish_slot_before_initialization": (0x14C4, "89 75 00"),
    "zero_228_byte_record": (0x14CF, "b9 39 00 00 00 33 c0 8b fe f3 ab"),
    "primary_or_nested_initializer_call": (0x14F9, "e8 c2 0a 00 00"),
    "initializer_failure_branch": (0x1501, "85 c0 0f 85 81 00 00 00"),
    "allocate_auxiliary_record": (0x1557, "e8 e4 0a 00 00"),
    "attach_auxiliary_to_primary": (0x1569, "89 8e c4 00 00 00"),
    "switch_to_auxiliary_record": (0x1572, "8b 74 24 14"),
    "loop_second_initializer": (0x1585, "e9 45 ff ff ff"),
    "failure_clears_handle": (0x158A, "c7 45 00 00 00 00 00"),
    "success_writes_output_handle": (0x1599, "8b 44 24 34 89 28"),
    "only_new_primary_increments_count": (0x159F, "8b 44 24 20 85 c0 75 06 ff 05 c4 4e 02 10"),
    "success_returns_zero": (0x15AD, "33 c0 5f 5e 5d 5b 83 c4 0c c3"),
    "auxiliary_one_parent_validation": (0x218E, "e8 ed 02 00 00"),
    "auxiliary_one_resets_child": (0x21D1, "b9 39 00 00 00 33 c0 f3 ab"),
    "auxiliary_one_uses_parent_handle": (0x21E5, "8b 96 d4 00 00 00"),
    "auxiliary_one_initializer": (0x21F3, "e8 c8 fd ff ff"),
    "auxiliary_two_parent_validation": (0x2222, "e8 59 02 00 00"),
    "auxiliary_two_resets_fixed_child": (0x2279, "bf a0 65 0e 10 f3 ab"),
    "auxiliary_two_uses_parent_handle": (0x2295, "8b 8e d4 00 00 00"),
    "auxiliary_two_initializer": (0x22A3, "e8 18 fd ff ff"),
}

# These original relative instructions establish the linked control-flow
# edges; matches are checked on exact hash and PE metadata first.
RELATIVE_EDGES = {
    "primary_or_nested_initializer_call": (0x1FC0, 1, 4),
    "initializer_failure_branch": (0x158A, 4, 4),
    "allocate_auxiliary_record": (0x2040, 1, 4),
    "loop_second_initializer": (0x14CF, 1, 4),
    "auxiliary_one_initializer": (0x1FC0, 1, 4),
    "auxiliary_two_initializer": (0x1FC0, 1, 4),
}


def static_audit(data: bytes) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if digest != OWNER_ORIGINAL_SHA256:
        raise PEFormatError("original APMMX SHA-256 mismatch")
    pe = PE32(data)
    if (pe.machine != 0x14C or pe.timestamp != 0x38076F98
            or pe.image_base != 0x10000000 or pe.image_size != 0xEE000):
        raise PEFormatError("original APMMX PE metadata mismatch")
    for name, (rva, hex_bytes) in SITES.items():
        expected = bytes.fromhex(hex_bytes)
        if pe.read_rva(rva, len(expected)) != expected:
            raise PEFormatError(f"original allocator opcode mismatch: {name} at 0x{rva:X}")
    for name, (target, displacement_offset, displacement_size) in RELATIVE_EDGES.items():
        rva, _ = SITES[name]
        disp = int.from_bytes(pe.read_rva(rva + displacement_offset, displacement_size),
                              "little", signed=True)
        instr_length = displacement_offset + displacement_size
        if rva + instr_length + disp != target:
            raise PEFormatError(f"original allocator branch target mismatch: {name}")
    return {
        "original_sha256": digest,
        "verified_original_opcode_anchors": len(SITES),
        "verified_original_control_flow_edges": len(RELATIVE_EDGES),
        "primary_top_level_allocator_rva": "0x13A0",
        "successful_handle_output_rva": "0x1599",
        "failure_invalidates_handle_rva": "0x158A",
        "in_place_reuse_preserves_handle_and_live_count": True,
        "one_primary_allocation_can_invoke_initializer_more_than_once": True,
        "auxiliary_inits_with_parent_handle_rvas": ["0x21F3", "0x22A3"],
        "initializer_call_is_not_an_epoch": True,
        "successful_publication_not_proof_of_physical_stream_generation": True,
        "permission_to_modify_original_audio": False,
        "caveat": "Static primary/auxiliary control flow only; indirect paths, locks, "
                  "other resource owners and concurrent lifetimes have not been proven.",
    }


class SyntheticEpochModel:
    """Conceptual safety oracle; native original has no proven epoch counter."""
    def __init__(self):
        self.serial = 0
        self.handles: dict[int, int] = {}

    def primary_commit(self, handle: int) -> int:
        self.serial += 1
        self.handles[handle] = self.serial
        return self.serial

    def auxiliary_init(self, handle: int) -> None:
        if handle not in self.handles:
            raise ValueError("unknown primary handle")
        # Auxiliary child may use the parent's handle without replacing voice.

    def failed_destructive_reinit(self, handle: int) -> None:
        # Original allocator may already have zeroed the record, and its
        # failure path explicitly clears the bound handle. Do not preserve it.
        self.handles.pop(handle, None)

    def retire(self, handle: int) -> None:
        self.handles.pop(handle, None)

    def unknown_lifecycle_event(self) -> None:
        self.handles.clear()

    def matches_conceptual_epoch(self, handle: int, observed: int) -> bool:
        return self.handles.get(handle) == observed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_dll", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = static_audit(args.original_dll.read_bytes())
    except (OSError, PEFormatError, OverflowError) as exc:
        print(f"APMMX primary-epoch audit refused: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Verified {report['verified_original_opcode_anchors']} exact opcode anchors "
              f"and {report['verified_original_control_flow_edges']} relative edges")
        print("Multiple initializer calls can belong to ONE primary allocation.")
        print("NO-GO: cannot identify a physical voice generation from initializer calls.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
