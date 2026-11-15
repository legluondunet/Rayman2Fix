#!/usr/bin/env python3
"""Original Rayman 2 ADPCM handle-lifetime audit (research only).

Read-only, SHA256-pinned static opcode verification of the owner's
1999 APMMXbvr.dll. Pure Python model demonstrates why a pointer + pool
ordinal + backlink + live count cannot certify a sound *generation*.

NEVER loads or changes a game process/DLL and NEVER authorizes a native
Pause/Resume interception. The model's conceptual generation is NOT a
field the original driver is known to store.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from audit_apmmxbvr import PE32, PEFormatError, OWNER_ORIGINAL_SHA256

# Exact original owner DLL RVA fingerprints verified by read-only disassembly.
# Distinguish the normal free-slot route from the in-place recycle route,
# both of which write [handle] = record at 0x14C4.
SITES = (
    ("max_30_live", 0x13A8, "83 f8 1e"),
    ("first_free_handle_table", 0x14A1, "8b 0d 48 4e 02 10"),
    ("first_free_slot_iteration", 0x14B0, "8b 4d 04 83 c5 04"),
    ("recycle_existing_record_handle", 0x14BE, "8b ae d4 00 00 00"),
    ("bind_handle_to_record", 0x14C4, "89 75 00"),
    ("reinitialize_record", 0x14CF, "b9 39 00 00 00 33 c0 8b fe f3 ab"),
    ("increment_count_new_record_only", 0x15A7, "ff 05 c4 4e 02 10"),
    ("invalidate_stopped_handle", 0x1655, "c7 06 00 00 00 00"),
    ("begin_all_handle_slot_rebase", 0x165B, "b8 48 4e 02 10"),
    ("rebase_moved_record_pointer_by_0xe4", 0x1666, "81 c1 1c ff ff ff"),
    ("write_rebased_pointer", 0x166C, "89 08"),
    ("handle_table_end_30_entries", 0x1671, "3d c0 4e 02 10"),
    ("compact_record_array", 0x16C0, "e8 9b 49 00 00"),
    ("record_becomes_inactive", 0x17CA, "89 53 28"),
    ("initialize_record_active", 0x2007, "c7 40 28 01 00 00 00"),
    ("record_backref_to_handle", 0x200E, "89 90 d4 00 00 00"),
)


def static_audit(data: bytes, *, expected_sha256: str=OWNER_ORIGINAL_SHA256) -> dict:
    digest=hashlib.sha256(data).hexdigest()
    if digest != expected_sha256:
        raise PEFormatError("original APMMXbvr.dll SHA-256 mismatch; no RVA conclusions")
    pe=PE32(data)
    if (pe.machine!=0x14C or pe.timestamp!=0x38076F98
        or pe.image_base!=0x10000000 or pe.image_size!=0xEE000):
        raise PEFormatError("original APMMXbvr.dll PE version mismatch")
    sites=[]
    for name,rva,expected in SITES:
        opcode=bytes.fromhex(expected)
        if pe.read_rva(rva,len(opcode))!=opcode:
            raise PEFormatError(f"original APMMX opcode mismatch: {name} at 0x{rva:X}")
        sites.append({"name":name,"rva":f"0x{rva:05X}","hex":expected.upper()})
    return {
        "file_sha256":digest,
        "file_bytes":len(data),
        "original_sites_verified":len(sites),
        "max_handle_slots":30,
        "record_stride_bytes":0xE4,
        "slot_table_start_original_va":"0x10024E48",
        "slot_table_end_original_va":"0x10024EC0",
        "verified_sites":sites,
        "claims":[
            "Original stop writes zero into the handle's record pointer.",
            "After stop, every surviving slot referencing a displaced record is rebased by 0xE4.",
            "Original creation chooses the first zero handle slot when using a new record.",
            "Eligible inactive records may be reinitialized in place with the SAME handle.",
            "A matching handle/record backlink/count is not proof of the same sound generation.",
        ],
        "permission_to_change_original_audio":False,
    }


@dataclass
class _Record:
    # The token below is a MODEL-ONLY witness of identity, never assumed
    # to exist as a native original ADPCM generation field.
    token: str
    slot: int
    active: bool=True


class HandleTableModel:
    """Minimal deterministic 30-slot/0xE4-stride driver lifetime model.

    Only mimics original *address allocation / retirement / compaction /
    eligible inactive-record reuse*. Does not model playback, focus, mixer
    locks or guaranteed eligibility of any particular completed record.
    """

    LIMIT=30
    STRIDE=0xE4

    def __init__(self):
        self.records: list[_Record]=[]
        self.slots: list[int|None]=[None]*self.LIMIT

    def play_new(self, token: str) -> int:
        if len(self.records)>=self.LIMIT:
            raise ValueError("original driver has at most 30 live records")
        slot=next(i for i,v in enumerate(self.slots) if v is None)
        self.slots[slot]=len(self.records)
        self.records.append(_Record(token,slot,True))
        self.validate()
        return slot

    def mark_reusable(self, slot: int) -> None:
        idx=self.slots[slot]
        if idx is None:
            raise ValueError("cannot complete an unbound handle")
        self.records[idx].active=False

    def play_in_place(self, token: str, slot: int) -> int:
        # Original 0x1460..0x14BE selects an eligible record with
        # active flag +0x28 == 0 (plus original +0xD8 conditions),
        # obtains its +0xD4 handle and initializes it anew.
        # Caller explicitly chooses an eligible record in this model.
        idx=self.slots[slot]
        if idx is None or self.records[idx].active:
            raise ValueError("no eligible existing inactive record")
        self.records[idx]=_Record(token,slot,True)
        self.validate()
        return slot

    def stop(self, slot: int) -> None:
        idx=self.slots[slot]
        if idx is None:
            raise ValueError("handle already retired")
        self.slots[slot]=None
        self.records.pop(idx)
        # Matches the original 30-entry sweep at 0x1660..0x1676.
        for i,other in enumerate(self.slots):
            if other is not None and other>idx:
                self.slots[i]=other-1
        self.validate()

    def observable(self, slot: int) -> tuple[int,int,int,bool]:
        """(stable slot address ordinal, pool index, live count, backlink ok).

        This models all fields currently trusted in a single runtime
        read-only snapshot, not the separate SXD Theme provenance.
        """
        idx=self.slots[slot]
        if idx is None:
            return (slot,-1,len(self.records),False)
        return (slot,idx,len(self.records),self.records[idx].slot==slot)

    def validate(self) -> None:
        if not 0<=len(self.records)<=self.LIMIT:
            raise AssertionError("record pool overrun")
        if sum(slot is not None for slot in self.slots)!=len(self.records):
            raise AssertionError("number of handles differs from record count")
        for idx,rec in enumerate(self.records):
            if self.slots[rec.slot]!=idx:
                raise AssertionError("original handle/record backlink broken")


def demonstrate_aliases() -> dict:
    # SAME pointer/ordinal/count/backlink for completely distinct music
    # from two different original pathways.
    a=HandleTableModel()
    old=a.play_new("track A")
    before=a.observable(old)
    a.stop(old)
    fresh=a.play_new("track B")
    after=a.observable(fresh)
    assert old==fresh and before==after
    stopped_alias={"before":before,"after":after,
                   "same_original_slot":old==fresh,
                   "different_logical_tracks":True}

    b=HandleTableModel()
    old=b.play_new("track A")
    before=b.observable(old)
    b.mark_reusable(old)
    reused=b.play_in_place("track B",old)
    after=b.observable(reused)
    assert old==reused and before==after
    in_place_alias={"before":before,"after":after,
                    "same_original_slot":old==reused,
                    "different_logical_tracks":True,
                    "native_stop_required":False}

    c=HandleTableModel()
    removed=c.play_new("track A")
    surviving=c.play_new("track B")
    old_index=c.slots[surviving]
    c.stop(removed)
    assert c.slots[surviving]==old_index-1
    survivor={"stable_handle_slot":surviving,
              "old_record_index":old_index,
              "new_record_index":c.slots[surviving],
              "backlink_valid":c.observable(surviving)[3]}
    return {
        "illustrative_model_only":True,
        "stop_then_new_voice_can_reuse_same_handle_and_ordinal":stopped_alias,
        "inactive_record_can_be_reinitialized_without_stop":in_place_alias,
        "surviving_handles_rebased_after_compaction":survivor,
        "rule":"No stable native generation token demonstrated; NEVER authorize delayed Resume from a pointer snapshot.",
    }


def main(argv: list[str]|None=None) -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("original_apmmxbvr_dll",type=Path)
    ap.add_argument("--json",action="store_true")
    args=ap.parse_args(argv)
    try:
        result=static_audit(args.original_apmmxbvr_dll.read_bytes())
    except (OSError,ValueError,OverflowError) as exc:
        print(f"ADPCM lifetime audit refused: {exc}",file=sys.stderr)
        return 2
    result["model"]=demonstrate_aliases()
    if args.json:
        print(json.dumps(result,indent=2))
    else:
        print(f"Original APMMX SHA256: {result['file_sha256']}")
        print(f"Verified original x86 sites: {result['original_sites_verified']}")
        for claim in result["claims"]:
            print("  "+claim)
        print("  TWO possible same-pointer/different-generation aliases, one WITHOUT Stop")
        print("  MODELS ONLY, NO REAL AUDIO COMMANDS OR STREAM GENERATION PROOF")
    return 0


if __name__=="__main__":
    sys.exit(main())
