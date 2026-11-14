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

# Original CD's entire 64-byte 0x453F90 resource resolver, obtained
# independently from the pinned protected CD and the research reference.
# It selects bank=ref>>16 and resource_array[ref&0xFFFF] (92-byte stride),
# checks bounds, and DOES NOT compare the returned record's +0 Id field.
# Store a digest of the code; never publish the proprietary function bytes.
RESOURCE_RESOLVER_VA = 0x453F90
RESOURCE_RESOLVER_SIZE = 64
RESOURCE_RESOLVER_SHA256 = (
    "1f4e185a18948dc27b9fe2af926984dbe0f4e4cbc846d66a9b2ff03b11b57c17"
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
    # Original-CD bank registry and resource resolver share these fields.
    # Original-CD type registry: three 12-byte list anchors.
    # The third begins at 0x5013F8 (personal type list). 0x407B00
    # walks its next-element (+0) and obtains the name from element+0xC.
    # The world roots are engine globals, not UI state booleans.
    ("menu_personal_list_initializer", 0x403C90,
     bytes.fromhex("BA F8 13 50 00")),
    ("menu_personal_list_indexer", 0x407B04,
     bytes.fromhex("A1 F8 13 50 00")),
    ("menu_personal_next_pointer", 0x407B0D,
     bytes.fromhex("8B 00")),
    ("menu_personal_name_pointer", 0x407B16,
     bytes.fromhex("8B 40 0C")),
    ("menu_dynamic_world_global", 0x403B75,
     bytes.fromhex("A3 D0 0F 50 00")),
    ("menu_inactive_world_global", 0x403B7F,
     bytes.fromhex("A3 C4 0F 50 00")),
    ("loaded_bank_list_head", 0x4538C0, bytes.fromhex("A1 50 4D 51 00")),
    ("loaded_bank_id_field", 0x4538CD, bytes.fromhex("39 48 04")),
    ("loaded_bank_next_field", 0x4538D2, bytes.fromhex("8B 40 3C")),
    ("loaded_bank_descriptor_bytes", 0x4538E2, bytes.fromhex("6A 48")),
    ("loaded_bank_head_pointer", 0x453FB2, bytes.fromhex("8B 40 24")),
    ("loaded_bank_resource_count", 0x453FB5, bytes.fromhex("3B 70 10")),
    ("loaded_bank_resource_first", 0x453FBA, bytes.fromhex("8B 50 0C")),
    ("bank_id_store", 0x453945, bytes.fromhex("89 46 04")),
    ("bank_filename_literal", 0x4A252C, b"bnk_%d.BNM\x00"),
)

# These are GENERIC SCRIPT VM OPCODES, not menu or keyboard events.
# 0x471E40 reads [esi-8] and subtracts 0x1AA. The three consecutive
# entries of the 8-byte VM opcode table point to the same dispatcher.
# The script interpreter's opcode pointer lookup is at 0x482690.
SCRIPT_OPCODE_TABLE_VA = 0x4A43E0
SCRIPT_AUDIO_OPCODE_IDS = (0x1AA, 0x1AB, 0x1AC)
SCRIPT_DISPATCHER_VA = 0x471E40
SCRIPT_MACHINE_ANCHORS = (
    ("vm_opcode_ptr_lookup", 0x482690,
     bytes.fromhex("8B 44 24 04 8B 04 C5 E0 43 4A 00 C3")),
    ("script_reads_opcode_from_bytecode", 0x471E40,
     bytes.fromhex("56 8B 74 24 0C 8B 46 F8")),
    ("script_subtracts_opcode_1aa", 0x471E48,
     bytes.fromhex("2D AA 01 00 00 74 21 48 74 15 48 75 20")),
    ("script_other_audio_opcode", 0x471E55,
     bytes.fromhex("A1 8C F7 4F 00 50 E8 20 12 FE FF")),
)
SCRIPT_OPCODE_BRANCHES = (
    ("generic_script_pause", 0x1AA, 0x471E70, 0x4536D0),
    ("generic_script_resume", 0x1AB, 0x471E67, 0x453700),
    ("generic_script_other_audio", 0x1AC, 0x471E5B, 0x453080),
)


def verify_script_opcode_dispatch(pe: "PE32") -> dict:
    """Verify the VM table and three branches; cannot prove menu identity."""
    anchors = all(pe.at(va, len(b)) == b for _, va, b in SCRIPT_MACHINE_ANCHORS)
    entries = [
        struct.unpack("<I", pe.at(SCRIPT_OPCODE_TABLE_VA + opcode*8, 4))[0]
        for opcode in SCRIPT_AUDIO_OPCODE_IDS
    ]
    flags = [
        struct.unpack("<I", pe.at(SCRIPT_OPCODE_TABLE_VA + opcode*8+4, 4))[0]
        for opcode in SCRIPT_AUDIO_OPCODE_IDS
    ]
    branches = all(pe.call_target(site) == target
                   for _, _opcode, site, target in SCRIPT_OPCODE_BRANCHES)
    return {
        "verified_native_dispatcher": anchors and
            entries == [SCRIPT_DISPATCHER_VA]*3 and flags == [0]*3
            and branches,
        "opcodes": list(SCRIPT_AUDIO_OPCODE_IDS),
        "shared_native_handler_va": f"0x{SCRIPT_DISPATCHER_VA:08X}",
        "script_opcode_table_base_va": f"0x{SCRIPT_OPCODE_TABLE_VA:08X}",
        "script_origin_alone_proves_esc_menu": False,
        "script_origin_alone_proves_f1_menu": False,
        "script_origin_alone_proves_gameplay_state": False,
        "script_caller_or_menu_ui_state_observation_needed": True,
    }


# ACP_Ray2 names the first byte as AI_g_bInGameMenu (AI.c). This is
# NOT yet evidence of the ESC menu specifically or F1 exclusion. The second
# byte is the engine's separate pause flag (GAM.c / 0x405CC0, 0x405CE0).
AI_INGAME_MENU_FLAG_VA = 0x4B9BC8
AI_INGAME_MENU_XREFS = (
    0x40376C, 0x40B004, 0x44B1C5, 0x44B25E, 0x44B31A,
    0x44B337, 0x44B3B7, 0x46082E, 0x460EF3, 0x4730CE,
)
ENGINE_PAUSE_FLAG_VA = 0x500640
ENGINE_PAUSE_XREFS = (0x405CC7, 0x405CE7, 0x405D01)
WINDOW_PAUSE_TITLE_VA = 0x49F0A8
WINDOW_PAUSE_TITLE_XREFS = (0x401767, 0x401A19)
AI_MENU_VM_OPCODE_ID = 0x1F8
AI_MENU_VM_HANDLER_VA = 0x4730A0
AI_MENU_VM_ENTRY_VA = 0x4A43E0 + 8 * AI_MENU_VM_OPCODE_ID
# Menu flag switches the game's own camera/viewport path in the display
# pipeline; verified against both owner-provided executables. Neither
# branch identifies ESC vs F1 or an actually visible menu.
MENU_RENDER_BRANCH_BYTES = bytes.fromhex(
    "A0 C8 9B 4B 00 83 C4 18 84 C0 74 13"
    " 68 D4 04 50 00 E8 27 94 01 00"
    " 83 C4 04 5F 5E 83 C4 14 C3"
    " 68 58 04 50 00 56 57 E8 42 EF 01 00"
    " 83 C4 0C 5F"
)


MENU_BG_HANDLE_VA = 0x4A7250
MENU_BG_LITERAL_XREFS = (0x404071, 0x4245D0)


def inspect_menu_background_handle(pe: "PE32") -> dict:
    """Validate render-resource handle references, not UI/menu presence."""
    initializer = pe.at(0x40406A, 11) == bytes.fromhex(
        "8B 48 14 8B 11 89 15 50 72 4A 00")
    renderer = pe.at(0x4245C8, 20) == bytes.fromhex(
        "8B 0D AC 36 50 00 8B 15 50 72 4A 00 "
        "51 52 FF 15 54 48 50 00")
    literal_xrefs = tuple(absolute_dword_xrefs(pe, MENU_BG_HANDLE_VA))
    return {
        "matched": initializer and renderer
                   and literal_xrefs == MENU_BG_LITERAL_XREFS,
        "handle_va": "0x004A7250",
        "only_two_direct_literal_xrefs": literal_xrefs == MENU_BG_LITERAL_XREFS,
        "initialization_is_not_menu_open": True,
        "render_use_is_not_proof_of_ESC": True,
        "render_use_is_not_proof_of_F1": True,
        "does_not_justify_audio_interception": True,
    }


def verify_native_menu_viewport_branch(pe: "PE32") -> bool:
    return (
        pe.at(0x40B003, len(MENU_RENDER_BRANCH_BYTES))
        == MENU_RENDER_BRANCH_BYTES
        and pe.call_target(0x40B014) == 0x424440
        and pe.call_target(0x40B029) == 0x429F70
    )

INGAME_MENU_CODE_ANCHORS = (
    ("initialise_false", 0x40376A, bytes.fromhex("C6 05 C8 9B 4B 00 00")),
    ("load_VM_opcode_and_compare_1F8", 0x4730A0,
     bytes.fromhex("8B 44 24 08 83 EC 10 81 78 F8 F8 01 00 00")),
    ("evaluate_VM_boolean_argument", 0x4730BB,
     bytes.fromhex("E8 D0 DA 00 00")),
    ("normalize_boolean_and_store", 0x4730C7,
     bytes.fromhex("85 C9 0F 95 C1 88 0D C8 9B 4B 00")),
    ("engine_pause_flag_set", 0x405CC0,
     bytes.fromhex("A1 A0 4E 51 00 C6 05 40 06 50 00 01")),
    ("engine_pause_flag_clear", 0x405CE0,
     bytes.fromhex("A1 E8 6A 4A 00 C6 05 40 06 50 00 00")),
    ("engine_pause_flag_getter", 0x405D00,
     bytes.fromhex("A0 40 06 50 00")),
    ("window_title_pause_call_1", 0x401766,
     bytes.fromhex("68 A8 F0 49 00 56 FF 15 2C C2 49 00")),
    ("window_title_pause_call_2", 0x401A18,
     bytes.fromhex("68 A8 F0 49 00 56 FF 15 2C C2 49 00")),
)


def inspect_native_menu_indicators(pe: "PE32") -> dict:
    """Compare only static relationships: never assert ESC or F1 UI state."""
    byte_anchors_valid = all(
        pe.at(va, len(signature)) == signature
        for _, va, signature in INGAME_MENU_CODE_ANCHORS
    )
    entry = pe.at(AI_MENU_VM_ENTRY_VA, 8)
    vm_table_valid = (
        struct.unpack_from("<II", entry) == (AI_MENU_VM_HANDLER_VA, 0)
        and pe.call_target(0x4730BB) == 0x480B90
    )
    menu_xrefs = tuple(absolute_dword_xrefs(pe, AI_INGAME_MENU_FLAG_VA))
    engine_xrefs = tuple(absolute_dword_xrefs(pe, ENGINE_PAUSE_FLAG_VA))
    title_xrefs = tuple(absolute_dword_xrefs(pe, WINDOW_PAUSE_TITLE_VA))
    title_valid = pe.at(WINDOW_PAUSE_TITLE_VA,
                        len(b"Rayman2 - pause" + bytes([0]))) == b"Rayman2 - pause" + bytes([0])
    all_checks = (
        byte_anchors_valid and vm_table_valid
        and menu_xrefs == AI_INGAME_MENU_XREFS
        and engine_xrefs == ENGINE_PAUSE_XREFS
        and title_xrefs == WINDOW_PAUSE_TITLE_XREFS
        and title_valid
    )
    return {
        "native_relationships_verified": all_checks,
        "ai_ingame_menu_byte_va": f"0x{AI_INGAME_MENU_FLAG_VA:08X}",
        "ai_ingame_menu_read_sites": len(AI_INGAME_MENU_XREFS) - 2,
        "ai_ingame_menu_direct_init_and_vm_writer_sites": 2,
        "script_vm_flag_set_opcode": f"0x{AI_MENU_VM_OPCODE_ID:X}",
        "script_vm_flag_set_handler": f"0x{AI_MENU_VM_HANDLER_VA:08X}",
        "engine_pause_byte_va": f"0x{ENGINE_PAUSE_FLAG_VA:08X}",
        "engine_pause_is_separate_from_ai_ingame_menu": True,
        "win32_pause_window_title_is_not_a_menu_state": True,
        "ai_ingame_menu_does_not_prove_ESC_specific": True,
        "ai_ingame_menu_does_not_exclude_F1": True,
        "gameplay_menu_lifetime_requires_independent_validation": True,
        "vm_table_valid": vm_table_valid,
        "byte_anchors_valid": byte_anchors_valid,
        "menu_xrefs_verified": menu_xrefs == AI_INGAME_MENU_XREFS,
        "engine_xrefs_verified": engine_xrefs == ENGINE_PAUSE_XREFS,
        "title_xrefs_verified": title_xrefs == WINDOW_PAUSE_TITLE_XREFS and title_valid,
    }


# Exhaustive bytewise E8-rel32 *candidates* in .text for selected known
# original-CD targets. These are not a complete control-flow graph: indirect
# calls and instruction decoding still require separate analysis.
DIRECT_CALLERS = (
    ("shared_pause_wrapper", 0x4536D0, (0x401C9C, 0x471E70)),
    ("shared_resume_wrapper", 0x453700, (0x401CE9, 0x471E67)),
    ("global_pause_loop", 0x4580B0, (0x4536ED,)),
    ("global_resume_loop", 0x458110, (0x45371D,)),
    ("inner_transition_pause", 0x4584A0, (0x4580E1, 0x459C4D)),
    ("inner_transition_resume", 0x458470, (0x458151, 0x459C6D)),
)


def direct_e8_candidate_sites(pe, target: int, section: str = ".text") -> list[int]:
    """Scan E8 rel32 encodings; each candidate still needs disassembly."""
    if not (0 <= target <= 0xFFFFFFFF):
        raise ValueError("Invalid x86 target VA")
    if section not in pe.sections:
        raise ValueError("Section not found")
    base, raw_pos, raw_size, _ = pe.sections[section]
    data = pe.data[raw_pos:raw_pos + raw_size]
    sites = []
    for idx in range(len(data) - 4):
        if data[idx] != 0xE8:
            continue
        rel = struct.unpack_from("<i", data, idx + 1)[0]
        if base + idx + 5 + rel == target:
            sites.append(base + idx)
    return sites


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
    direct_call_candidates = [
        {"name": name, "target_va": f"0x{target:08X}",
         "confirmed_bytewise_both": (
             tuple(direct_e8_candidate_sites(cd, target)) == expected
             and tuple(direct_e8_candidate_sites(ref, target)) == expected),
         "number_of_known_direct_e8_sites": len(expected)}
        for name, target, expected in DIRECT_CALLERS
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
    script_opcode = {
        "cd": verify_script_opcode_dispatch(cd),
        "reference": verify_script_opcode_dispatch(ref),
    } if check_anchors else {}
    script_opcode_verified = bool(check_anchors and
        all(x["verified_native_dispatcher"] for x in script_opcode.values()))
    menu_indicators = {
        "cd": inspect_native_menu_indicators(cd),
        "reference": inspect_native_menu_indicators(ref),
    } if check_anchors else {}
    menu_indicators_verified = bool(check_anchors and
        all(x["native_relationships_verified"]
            for x in menu_indicators.values()))
    menu_render_verified = bool(check_anchors and
        verify_native_menu_viewport_branch(cd) and
        verify_native_menu_viewport_branch(ref))
    menu_background = {
        "cd": inspect_menu_background_handle(cd),
        "reference": inspect_menu_background_handle(ref),
    } if check_anchors else {}
    menu_background_verified = bool(check_anchors and
        all(x["matched"] for x in menu_background.values()))


    # Synthetic PE tests intentionally skip proprietary native anchors.
    # Do not read addresses absent from those small synthetic fixtures.
    if check_anchors:
        native_resource_lookup_verified = (
            hashlib.sha256(cd.at(
                RESOURCE_RESOLVER_VA, RESOURCE_RESOLVER_SIZE)).hexdigest()
                == RESOURCE_RESOLVER_SHA256
            and hashlib.sha256(ref.at(
                RESOURCE_RESOLVER_VA, RESOURCE_RESOLVER_SIZE)).hexdigest()
                == RESOURCE_RESOLVER_SHA256
        )
    else:
        native_resource_lookup_verified = False
    rdata_identical = cd.section(".rdata") == ref.section(".rdata")
    verified_windows = all(w["identical"] for w in windows)
    verified_native_anchors = (
        all(x["verified_both"] for x in
            call_sites + byte_anchors + dispatcher_pointers + transaction_ops)
        and all(x["confirmed_bytewise_both"] for x in direct_call_candidates)
        and transaction_xrefs_verified and callback_registration_verified
        and script_opcode_verified and menu_render_verified
        and menu_background_verified
        and native_resource_lookup_verified
    ) if check_anchors else False
    # A caller may disable native anchors for synthetic PE parser tests only.
    # That MUST NOT accidentally yield a full original-CD audio-path proof.
    all_checks = verified_windows and (not check_anchors or verified_native_anchors)
    return {"cd_sha256": cd_digest, "reference_sha256": ref_digest,
            "exact_pinned_pair": pinned, "selected_audio_code_equal": all_checks,
            "original_cd_audio_path_confirmed": pinned and all_checks and check_anchors,
            "original_cd_bank_filename_formatter_verified": pinned and bank_checked,
            "menu_actor_readonly_source_map": {
                "pinned_original_cd_and_reference_anchors_verified": pinned
                    and check_anchors and all(x["verified_both"]
                        for x in byte_anchors
                        if x["name"].startswith("menu_personal_")
                        or x["name"].startswith("menu_dynamic_world_")
                        or x["name"].startswith("menu_inactive_world_")),
                "personal_type_list_head_va": "0x005013F8",
                "dynamic_world_global_va": "0x00500FD0",
                "inactive_world_global_va": "0x00500FC4",
                "list_element_next_offset": 0,
                "list_element_name_offset": 12,
                "actor_live_instance_not_yet_observed": True,
                "actor_presence_is_not_ESC_exclusivity_proof": True,
            },
            "native_loaded_bank_inverse_mapping": {
                "verified_pinned_cd_and_reference": pinned and
                    all(x["verified_both"] for x in byte_anchors if
                        x["name"].startswith("loaded_bank_")),
                "list_head_va": "0x00514D50",
                "bank_id_offset": 4,
                "next_bank_offset": 60,
                "bank_head_pointer_offset": 36,
                "resource_first_pointer_offset": 12,
                "resource_count_offset": 16,
                "resource_record_stride_bytes": 92,
                "does_not_rely_on_record_Id_halves": True,
                "live_loaded_bank_membership_not_observed_yet": True,
            },
            "native_resource_lookup": {
                "verified_pinned_cd_and_reference": pinned and
                    native_resource_lookup_verified,
                "function_va": "0x00453F90",
                "selects_bank_from_upper16": True,
                "selects_record_index_from_lower16": True,
                "record_stride_bytes": 92,
                "does_not_compare_returned_record_id_field": True,
                "record_id_is_not_proof_of_filename": True,
            },
            "rdata_raw_identical": rdata_identical,
            "rdata_raw_bytes": len(cd.section(".rdata")),
            "regions": windows, "call_sites": call_sites, "opcode_anchors": byte_anchors,
            "original_cd_render_menu_viewport_branch_verified":
                pinned and menu_render_verified,
            "native_menu_background_handle": {
                "verified_both": menu_background_verified,
                "cd": menu_background.get("cd", {}),
                "reference": menu_background.get("reference", {}),
                "does_not_prove_menu_open": True,
            },
            "native_menu_render_branch": {
                "verified_both": menu_render_verified,
                "flag_va": "0x004B9BC8",
                "when_menu_flag_nonzero_viewport_va": "0x005004D4",
                "when_menu_flag_zero_viewport_va": "0x00500458",
                "proves_ESC_only": False,
                "proves_menu_visible_at_audio_call": False,
            },
            "original_cd_menu_state_relationships_verified":
                pinned and menu_indicators_verified,
            "native_menu_indicator_audit": {
                "verified_both": menu_indicators_verified,
                "cd": menu_indicators.get("cd", {}),
                "reference": menu_indicators.get("reference", {}),
                "in_game_menu_is_not_proven_ESC_only": True,
                "does_not_justify_runtime_audio_change": True,
            },
            "resource_dispatcher_pointers": dispatcher_pointers,
            "script_vm_audio_opcode_dispatch": {
                "verified_both": script_opcode_verified,
                "opcode_ids": list(SCRIPT_AUDIO_OPCODE_IDS),
                "generic_script_pause_va": "0x00471E70",
                "generic_script_resume_va": "0x00471E67",
                "does_not_prove_ESC_menu": True,
                "does_not_prove_F1_menu": True,
            },
            "direct_call_candidate_audit": {
                "targets": direct_call_candidates,
                "indirect_calls_not_enumerated": True,
                "bytewise_scan_is_not_full_disassembly": True,
            },
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
