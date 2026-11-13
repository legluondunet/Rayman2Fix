#!/usr/bin/env python3
"""Read-only byte parity check for owner-supplied Rayman 2 CD and reference EXEs.

The listed known SHA-256 fingerprints are mandatory in normal CLI use.
No proprietary bytes are printed, extracted, modified, or embedded in tests.
This establishes selected code/data parity, NOT whole-program equivalence or
retail BNM asset identity. Not a game patch or a music-in-ESC implementation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

CD_SHA256 = "7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3"
REFERENCE_SHA256 = "9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713"

# Intervals are half-open VAs (not offsets). All independently read from each
# image's own PE section mapping, because CD / reference raw offsets differ.
AUDIO_WINDOWS = (
    ("win32_focus_callers", 0x401C50, 0x401D20),
    ("script_pause_callers", 0x471E20, 0x471EB0),
    ("global_pause_wrappers", 0x4536C0, 0x453740),
    ("global_pause_resume_loops", 0x458050, 0x458180),
    ("theme_controller_lifecycle", 0x4597D0, 0x459CC0),
    ("sound_type_dispatch", 0x457E40, 0x457FC0),
    ("native_sound_allocation", 0x4574D0, 0x457610),
    ("native_transition_playback", 0x458200, 0x4585A0),
    ("sxd_driver_wrappers", 0x455010, 0x4553E0),
    ("sound_bank_loader", 0x4538B0, 0x4540B0),
    ("sound_bank_opcode_loader", 0x450790, 0x450840),
    ("sound_creation_requests", 0x452D10, 0x453060),
    ("sound_event_dispatch", 0x45A230, 0x45B100),
)

# Exact x86 E8 call sites: validates both direct script and focus routes and
# their shared pause/resume wrappers, independently in CD and reference.
CALL_TARGETS = (
    ("win32_pause", 0x401C9C, 0x4536D0),
    ("win32_resume", 0x401CE9, 0x453700),
    ("script_pause", 0x471E70, 0x4536D0),
    ("script_resume", 0x471E67, 0x453700),
    ("wrapper_pause", 0x4536ED, 0x4580B0),
    ("wrapper_resume", 0x45371D, 0x458110),
    ("bank_filename_formatter", 0x453948, 0x453870),
    ("direct_inner_pause_dispatch", 0x4580E1, 0x4584A0),
    ("outer_resource_pause_dispatch", 0x4580EF, 0x458070),
    ("theme_delegated_pause", 0x459C4D, 0x4584A0),
    ("direct_inner_resume_dispatch", 0x458151, 0x458470),
    ("outer_resource_resume_dispatch", 0x45815F, 0x458090),
    ("theme_delegated_resume", 0x459C6D, 0x458470),
)

# Exact original-CD pause mark/depth branch order, rechecked in the ref.
ANCHOR_BYTES = (
    ("pause_mark_guard", 0x4580CA, bytes.fromhex("83 3E 00")),
    ("pause_depth_read", 0x4580CF, bytes.fromhex("A1 C8 3C 4A 00")),
    ("pause_mark_write", 0x4580D4, bytes.fromhex("89 06")),
    ("pause_depth_increment", 0x458100, bytes.fromhex("40")),
    ("pause_depth_store", 0x458102, bytes.fromhex("A3 C8 3C 4A 00")),
    ("resume_depth_decrement", 0x458116, bytes.fromhex("48")),
    ("resume_depth_store", 0x458118, bytes.fromhex("A3 C8 3C 4A 00")),
    ("resume_mark_compare", 0x45813C, bytes.fromhex("3B C8")),
    ("resume_mark_clear", 0x458143, bytes.fromhex("C7 06 00 00 00 00")),
    ("bank_id_store", 0x453945, bytes.fromhex("89 46 04")),
    ("bank_filename_literal", 0x4A252C, b"bnk_%d.BNM\x00"),
)

# Controller-wide dynamic guard during Theme creation/replacement and stop.
# This is a game-internal reentrancy/transition inhibit flag, NOT a mutex.
THEME_TRANSACTION_FLAG_VA = 0x4B9618
THEME_TRANSACTION_OPS = (
    ("init_clears_flag", 0x459694, bytes.fromhex("89 15 18 96 4B 00")),
    ("part_advance_reads_flag", 0x4596E0, bytes.fromhex("A1 18 96 4B 00 56 85 C0 75 67")),
    ("begin_theme_start", 0x45999D, bytes.fromhex("C7 05 18 96 4B 00 01 00 00 00")),
    ("end_theme_start", 0x459B2C, bytes.fromhex("C7 05 18 96 4B 00 00 00 00 00")),
    ("begin_theme_stop", 0x459BE5, bytes.fromhex("C7 05 18 96 4B 00 01 00 00 00")),
    ("end_theme_stop", 0x459C23, bytes.fromhex("C7 05 18 96 4B 00 00 00 00 00")),
)
# Addresses of the four-byte operand in each original-CD instruction.
THEME_FLAG_OPERAND_VAS = (0x459696, 0x4596E1, 0x45999F,
                          0x459B2E, 0x459BE7, 0x459C25)


def absolute_dword_xrefs(pe, target: int, section: str = ".text") -> list[int]:
    """Return VA of every literal 32-bit little-endian operand in one PE section.

    This is a deliberately syntax-agnostic byte scan; each hit must ALSO have
    a verified instruction around it before being interpreted as a code xref.
    """
    if not (0 <= target <= 0xFFFFFFFF):
        raise ValueError("Absolute target is not a 32-bit VA")
    if section not in pe.sections:
        raise ValueError("Section not present")
    base, raw_pos, raw_size, _ = pe.sections[section]
    data = pe.data[raw_pos:raw_pos + raw_size]
    pattern = struct.pack("<I", target)
    out = []
    start = 0
    while True:
        idx = data.find(pattern, start)
        if idx == -1:
            return out
        out.append(base + idx)
        start = idx + 1


# Type 11 native resource dispatcher has a 24-byte stride per kind;
# pause entry is at base+8 and resume at base+12 (indexed by 11).
DISPATCH_POINTERS = (
    ("theme_type11_pause", 0x4A3BA8 + 24 * 11, 0x459C40),
    ("theme_type11_resume", 0x4A3BAC + 24 * 11, 0x459C60),
)


def _u16(b: bytes, p: int) -> int:
    return struct.unpack_from("<H", b, p)[0]


def _u32(b: bytes, p: int) -> int:
    return struct.unpack_from("<I", b, p)[0]


class PE32:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x100 or data[:2] != b"MZ":
            raise ValueError("Invalid DOS/PE header")
        pe = _u32(data, 0x3C)
        if pe < 0x40 or pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
            raise ValueError("Invalid PE header offset/signature")
        count = _u16(data, pe + 6)
        optional_len = _u16(data, pe + 20)
        opt = pe + 24
        header_end = opt + optional_len
        if _u16(data, pe + 4) != 0x14C or optional_len < 0x60:
            raise ValueError("Not a supported x86 PE32 file")
        if header_end > len(data) or _u16(data, opt) != 0x10B:
            raise ValueError("Invalid PE32 optional header")
        self.image_base = _u32(data, opt + 0x1C)
        if not (1 <= count <= 96) or header_end + count * 40 > len(data):
            raise ValueError("Invalid PE section header table")
        self.sections = {}
        intervals = []
        for idx in range(count):
            pos = header_end + idx * 40
            name = data[pos:pos+8].split(b"\0", 1)[0].decode("ascii", "replace")
            vsize, rva, raw_size, raw_pos = struct.unpack_from("<IIII", data, pos+8)
            if name in self.sections or raw_pos + raw_size > len(data):
                raise ValueError("Duplicate or truncated PE section")
            self.sections[name] = (self.image_base + rva, raw_pos, raw_size, vsize)
            if raw_size:
                intervals.append((self.image_base + rva, self.image_base + rva + raw_size))
        intervals.sort()
        if any(intervals[i][1] > intervals[i+1][0] for i in range(len(intervals)-1)):
            raise ValueError("Overlapping virtual PE sections")

    def at(self, va: int, length: int) -> bytes:
        if va < self.image_base or length < 0:
            raise ValueError("Invalid VA or length")
        for base, raw_pos, size, _virtual_size in self.sections.values():
            delta = va - base
            if 0 <= delta and delta + length <= size:
                return self.data[raw_pos+delta:raw_pos+delta+length]
        raise ValueError("VA range not physically backed in a PE section")

    def section(self, name: str) -> bytes:
        if name not in self.sections:
            raise ValueError("Required PE section missing: " + name)
        _base, off, size, _virtual_size = self.sections[name]
        return self.data[off:off+size]

    def call_target(self, va: int) -> int | None:
        op = self.at(va, 5)
        if op[0] != 0xE8:
            return None
        return va + 5 + struct.unpack_from("<i", op, 1)[0]


def compare_regions(cd: PE32, ref: PE32, regions=AUDIO_WINDOWS) -> list[dict]:
    out = []
    for name, start, end in regions:
        if end <= start:
            raise ValueError("Empty/reversed analysis interval")
        a, b = cd.at(start, end - start), ref.at(start, end - start)
        # Never publish code bytes or file paths in the report.
        out.append({"name": name, "start_va": f"0x{start:08X}",
                    "end_exclusive_va": f"0x{end:08X}", "byte_count": len(a),
                    "identical": a == b, "cd_region_sha256": hashlib.sha256(a).hexdigest()})
    return out


def audit(cd_bytes: bytes, ref_bytes: bytes, *, require_pins: bool = True,
          regions=AUDIO_WINDOWS, check_anchors: bool = True) -> dict:
    cd_digest = hashlib.sha256(cd_bytes).hexdigest()
    ref_digest = hashlib.sha256(ref_bytes).hexdigest()
    pinned = cd_digest == CD_SHA256 and ref_digest == REFERENCE_SHA256
    if require_pins and not pinned:
        raise ValueError("Input fingerprints differ from known owner-provided CD/reference pair")
    cd, ref = PE32(cd_bytes), PE32(ref_bytes)
    if cd.image_base != 0x400000 or ref.image_base != 0x400000:
        raise ValueError("Unexpected executable image base")
    windows = compare_regions(cd, ref, regions)
    call_sites = []
    byte_anchors = []
    dispatcher_pointers = []
    if check_anchors:
        for name, addr, expected in CALL_TARGETS:
            call_sites.append({"name": name, "verified_both":
                               cd.call_target(addr) == expected and ref.call_target(addr) == expected})
        for name, addr, expected in ANCHOR_BYTES:
            byte_anchors.append({"name": name, "verified_both":
                                 cd.at(addr, len(expected)) == expected and
                                 ref.at(addr, len(expected)) == expected})
        for name, addr, target in DISPATCH_POINTERS:
            dispatcher_pointers.append({"name": name, "verified_both":
                _u32(cd.at(addr, 4), 0) == target and _u32(ref.at(addr, 4), 0) == target})
        byte_anchors.append({"name": "type_11_theme_dispatch", "verified_both":
                             struct.unpack("<I", cd.at(0x457FC8, 4))[0] == 0x457F21 and
                             struct.unpack("<I", ref.at(0x457FC8, 4))[0] == 0x457F21})
    bank_checked = (cd.call_target(0x453948) == 0x453870 and
                    ref.call_target(0x453948) == 0x453870 and
                    cd.at(0x453945, 3) == ref.at(0x453945, 3) == bytes.fromhex("89 46 04") and
                    cd.at(0x4A252C, 11) == ref.at(0x4A252C, 11) == b"bnk_%d.BNM\x00") if check_anchors else False
    transaction_ops = [
        {"name": name, "verified_both":
            cd.at(addr, len(expected)) == expected and
            ref.at(addr, len(expected)) == expected}
        for name, addr, expected in THEME_TRANSACTION_OPS
    ] if check_anchors else []
    cd_flag_xrefs = absolute_dword_xrefs(cd, THEME_TRANSACTION_FLAG_VA)
    ref_flag_xrefs = absolute_dword_xrefs(ref, THEME_TRANSACTION_FLAG_VA)
    transaction_xrefs_verified = (
        tuple(cd_flag_xrefs) == THEME_FLAG_OPERAND_VAS
        and tuple(ref_flag_xrefs) == THEME_FLAG_OPERAND_VAS)
    callback_registration_verified = (
        cd.at(0x459AE5, 5) == ref.at(0x459AE5, 5) ==
            bytes.fromhex("68 E0 96 45 00")
        and cd.call_target(0x459AEC) == 0x458220
        and ref.call_target(0x459AEC) == 0x458220
    ) if check_anchors else False
    rdata_identical = cd.section(".rdata") == ref.section(".rdata")
    verified_windows = all(w["identical"] for w in windows)
    verified_native_anchors = (
        all(x["verified_both"] for x in
            call_sites + byte_anchors + dispatcher_pointers + transaction_ops)
        and transaction_xrefs_verified and callback_registration_verified
    ) if check_anchors else False
    # A caller may disable native anchors for synthetic PE parser tests only.
    # That MUST NOT accidentally yield a full original-CD audio-path proof.
    all_checks = verified_windows and (not check_anchors or verified_native_anchors)
    return {"cd_sha256": cd_digest, "reference_sha256": ref_digest,
            "exact_pinned_pair": pinned, "selected_audio_code_equal": all_checks,
            "original_cd_audio_path_confirmed": pinned and all_checks and check_anchors,
            "original_cd_bank_filename_formatter_verified": pinned and bank_checked,
            "rdata_raw_identical": rdata_identical,
            "rdata_raw_bytes": len(cd.section(".rdata")),
            "regions": windows, "call_sites": call_sites, "opcode_anchors": byte_anchors,
            "resource_dispatcher_pointers": dispatcher_pointers,
            "theme_transaction_inhibit": {
                "verified_both": transaction_xrefs_verified and
                    callback_registration_verified and
                    all(x["verified_both"] for x in transaction_ops),
                "flag_va": f"0x{THEME_TRANSACTION_FLAG_VA:08X}",
                "all_six_operand_xrefs_match": transaction_xrefs_verified,
                "registered_part_advance_callback": callback_registration_verified,
                "operations": transaction_ops,
                "does_not_prove_thread_synchronization": True,
                "does_not_authorize_audio_interception": True,
            },
            "theme_inner_and_outer_pause_paths_byte_verified": pinned and all(
                x["verified_both"] for x in call_sites + dispatcher_pointers),
            "not_whole_executable_equivalence": True,
            "does_not_verify_retail_bnm_files_or_pause_patch": True}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("original_cd", type=Path)
    ap.add_argument("reference", type=Path)
    args = ap.parse_args()
    print(json.dumps(audit(args.original_cd.read_bytes(), args.reference.read_bytes()), indent=2))


if __name__ == "__main__":
    main()
