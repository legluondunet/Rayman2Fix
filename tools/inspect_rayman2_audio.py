#!/usr/bin/env python3
"""Read-only PE32/x86 structural checks for Rayman 2's native sound controller.

This tool does not run a game, modify an EXE, parse sound assets or prove that a
particular sound handle is soundtrack music. Known VAs are from a NON-APPROVED
1999 PC reference executable; the original protected CD needs separate proof.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

REFERENCE_SHA256 = "9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713"
EVENT_DISPATCH = 0x457E60
EVENT_TABLE = 0x457FA0
TYPE11_CONTROLLER = 0x459950
INNER_SOUND_CREATE = 0x458220
SXD_PLAY_WRAPPER = 0x455280
CONTROLLER_BASE = 0x4B95B8
CONTROLLER_INNER_SLOT = CONTROLLER_BASE + 0x30
PAUSE_DISPATCH_TABLE = 0x4A3BA8
RESUME_DISPATCH_TABLE = 0x4A3BAC


class PE32:
    """Minimal checked VA-to-file-offset PE reader. No external packages."""

    def __init__(self, raw: bytes):
        self.raw = raw
        if len(raw) < 0x100 or raw[:2] != b"MZ":
            raise ValueError("Not an MZ executable")
        nt, = self.unpack("<I", 0x3C)
        if nt > len(raw) - 24 or raw[nt:nt + 4] != b"PE\0\0":
            raise ValueError("Not a PE executable")
        machine, number = self.unpack("<HH", nt + 4)
        optional_size, = self.unpack("<H", nt + 20)
        opt = nt + 24
        magic, = self.unpack("<H", opt)
        if machine != 0x14C or magic != 0x10B:
            raise ValueError("Not an x86 PE32 executable")
        if optional_size < 0x60:
            raise ValueError("Truncated PE32 optional header")
        self.image_base, = self.unpack("<I", opt + 0x1C)
        self.timestamp, = self.unpack("<I", nt + 8)
        self.sections = []
        table = opt + optional_size
        for n in range(number):
            pos = table + 40 * n
            name = self.read(pos, 8).split(b"\0")[0].decode("ascii", "replace")
            vsize, va, raw_size, raw_pointer = self.unpack("<4I", pos + 8)
            self.sections.append(
                (name, va, max(vsize, raw_size), raw_size, raw_pointer)
            )

    def read(self, offset: int, length: int) -> bytes:
        if offset < 0 or length < 0 or offset + length > len(self.raw):
            raise ValueError("Out-of-file read")
        return self.raw[offset:offset + length]

    def unpack(self, fmt: str, offset: int):
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))

    def addr(self, va: int, size: int = 1) -> int:
        rva = va - self.image_base
        for _name, begin, span, file_size, start in self.sections:
            delta = rva - begin
            if 0 <= delta and delta + size <= span and delta + size <= file_size:
                self.read(start + delta, size)
                return start + delta
        raise ValueError(f"VA {va:#x} not backed by a file section")

    def at(self, va: int, size: int) -> bytes:
        return self.read(self.addr(va, size), size)

    def dword(self, va: int) -> int:
        return struct.unpack("<I", self.at(va, 4))[0]

    def call_target(self, va: int):
        # Exact instruction location only. Never scan arbitrary E8 bytes.
        raw = self.at(va, 5)
        if raw[:1] != b"\xE8":
            return None
        disp, = struct.unpack("<i", raw[1:])
        return (va + 5 + disp) & 0xFFFFFFFF

    def name_at(self, va: int, limit: int = 100) -> str:
        offset = self.addr(va)
        end = min(offset + limit, len(self.raw))
        data = self.read(offset, end - offset).split(b"\0", 1)[0]
        return data.decode("ascii", "replace")


def decode_resource_handle(handle: int) -> dict:
    """Decode the packed handle that native 0x453F90 accepts.

    Group ID indexes a *loaded runtime registry*, not a disc, archive or
    file number. An integer handle alone never establishes soundtrack ID.
    """
    if handle < 0 or handle > 0xFFFFFFFF:
        raise ValueError("Resource handle outside DWORD range")
    return {
        "group_id": handle >> 16,
        "record_index": handle & 0xFFFF,
        "record_stride": 92,
    }


def summarize_reference_file_directory(directory: bytes) -> dict:
    """Parse the reference EXE's length-prefixed file directory, read-only.

    Each entry is <u32 declared_size><u32 NUL-terminated name length><name>.
    Sentinel (0xFFFFFFFF,0) is followed by zero padding to section end.
    The declared sizes are *not verified disk lengths*; protected discs can
    contain intentionally implausible sizes. Never use for file integrity.
    """
    offset = 0
    count = 0
    declared_total = 0
    sound_ext_count = {"APM": 0, "BNM": 0}
    sound_ext_declared = {"APM": 0, "BNM": 0}
    seen = set()
    while offset + 8 <= len(directory):
        declared_size, path_len = struct.unpack_from("<II", directory, offset)
        offset += 8
        if declared_size == 0xFFFFFFFF and path_len == 0:
            if any(directory[offset:]):
                raise ValueError("Reference directory has nonzero padding")
            return {
                "entries": count,
                "unique_paths": len(seen),
                "sentinel_verified": True,
                "declared_bytes_total_not_integrity": declared_total,
                "sound_entries": sound_ext_count,
                "sound_declared_bytes_not_integrity": sound_ext_declared,
                "sizes_verified_against_files": False,
                "approved_cd_metadata": False,
            }
        if not (2 <= path_len <= 256) or offset + path_len > len(directory):
            raise ValueError("Invalid reference directory path length")
        encoded = directory[offset:offset + path_len]
        offset += path_len
        if encoded[-1] != 0 or 0 in encoded[:-1]:
            raise ValueError("Invalid reference directory terminator")
        name = encoded[:-1].decode("ascii")
        key = name.casefold()
        if key in seen:
            raise ValueError("Duplicate reference directory entry")
        seen.add(key)
        count += 1
        declared_total += declared_size
        suffix = name.rsplit(".", 1)[-1].upper()
        if suffix in sound_ext_count:
            sound_ext_count[suffix] += 1
            sound_ext_declared[suffix] += declared_size
    raise ValueError("Reference directory sentinel not found")


def summarize_gog_sound_manifest(directory: bytes) -> dict:
    """Names from the modified reference EXE only; never audio payloads."""
    paths = re.findall(
        rb"DATA\\WORLD\\SOUND\\([^\x00\r\n]{1,80}\.(?:APM|BNM))\x00",
        directory, flags=re.IGNORECASE,
    )
    names = [p.decode("ascii", "replace").upper() for p in paths]
    apms = {p for p in names if p.endswith(".APM")}
    bnms = {p for p in names if p.endswith(".BNM")}
    indexed_banks = {"BNK_%d.BNM" % i for i in range(405)}
    return {
        "apm_unique": len(apms),
        "bnm_unique": len(bnms),
        "bnk_0_to_404_complete": bnms == indexed_banks,
        "main_menu_mm01_to_mm10_present": all(
            ("MM%02d.APM" % i) in apms for i in range(1, 11)
        ),
        "intro_jailmus_present": "JAILMUS.APM" in apms,
        "main_title_present": "MAIN TITLE.APM" in apms,
        "contains_paths_not_sound_bytes": True,
        "applies_to_original_cd": False,
    }


def inspect(executable: Path) -> dict:
    raw = executable.read_bytes()
    pe = PE32(raw)
    sha = hashlib.sha256(raw).hexdigest()
    result = {
        "source": executable.name,
        "sha256": sha,
        "bytes": len(raw),
        "image_base": f"0x{pe.image_base:08X}",
        "pe_timestamp": pe.timestamp,
        "known_reference_only": sha == REFERENCE_SHA256,
        "not_approved_cd_proof": True,
        "sections": [name for name, *_ in pe.sections],
    }
    try:
        events = [pe.dword(EVENT_TABLE + 4 * i) for i in range(11)]
        result["resource_type11"] = {
            "dispatch_prologue_expected": pe.at(EVENT_DISPATCH, 6)
                == bytes.fromhex("53e87af6ffff"),
            "table_entries": len(events),
            "case11_target": f"0x{events[10]:08X}",
            "case11_verified": events[10] == 0x457F21,
            "resource_enum_source":
                "raytools/ACP_Ray2 src/SND/SND_Resource.h",
            "resource_type": "SND_E_TR_Theme",
            "enum_value": 11,
        }
        edges = [
            (0x457F38, TYPE11_CONTROLLER),
            (0x459AEC, INNER_SOUND_CREATE),
            (0x4582A5, SXD_PLAY_WRAPPER),
            (0x459C4D, 0x4584A0),
            (0x459C6D, 0x458470),
        ]
        result["direct_calls"] = [{
            "site": f"0x{site:08X}",
            "expected": f"0x{target:08X}",
            "observed": None if (obs := pe.call_target(site)) is None
                else f"0x{obs:08X}",
            "match": obs == target,
        } for site, target in edges]
        result["nested_type11"] = {
            "controller": f"0x{CONTROLLER_BASE:08X}",
            "inner_record_slot": f"0x{CONTROLLER_INNER_SLOT:08X}",
            "writes_nested_slot": pe.at(0x459AF4, 5)
                == bytes.fromhex("a3e8954b00"),
            "pause_reads_plus_30": pe.at(0x459C49, 3)
                == bytes.fromhex("8b4030"),
            "resume_reads_plus_30": pe.at(0x459C69, 3)
                == bytes.fromhex("8b4030"),
            "pause_controller_handler":
                f"0x{pe.dword(PAUSE_DISPATCH_TABLE + 24 * 11):08X}",
            "resume_controller_handler":
                f"0x{pe.dword(RESUME_DISPATCH_TABLE + 24 * 11):08X}",
        }
        # ACP_Ray2 maps the native values at [resource+4] to actual
        # *resource kinds*, NOT SND_tdeTypeEvent. Cross-check the native
        # SND_ResBank linked list and the resource layout against code.
        result["sound_bank_and_resource_layout"] = {
            "source": "https://github.com/raytools/ACP_Ray2/tree/master/src/SND",
            "bank_list_global": "0x00514D50",
            "bank_list_pointer": pe.at(0x4538C0, 5)
                == bytes.fromhex("a1504d5100"),
            "bank_id_offset": 4,
            "bank_id_cmp": pe.at(0x4538CD, 3)
                == bytes.fromhex("394804"),
            "bank_filename_offset": 12,
            "bank_data_pointer_offset": 36,
            "bank_next_pointer_offset": 60,
            "bank_next_link": pe.at(0x4538D2, 3)
                == bytes.fromhex("8b403c"),
            "bank_descriptor_bytes": 72,
            "bank_allocates_72_bytes": pe.at(0x4538E2, 2)
                == bytes.fromhex("6a48"),
            "bank_head_resource_first_offset": 12,
            "bank_head_resource_count_offset": 16,
            "bank_head_resource_first_read": pe.at(0x453FBA, 3)
                == bytes.fromhex("8b500c"),
            "bank_head_resource_count_cmp": pe.at(0x453FB5, 3)
                == bytes.fromhex("3b7010"),
            "resource_record_bytes": 92,
            "resource_fields": {
                "Id": 0,
                "eType": 4,
                "eStorage": 8,
                "ucVolume": 12,
                "bIsLoaded": 16,
                "uRes": 20,
            },
            "resource_types": {
                "SND_E_TR_Sample": 1,
                "SND_E_TR_Midi": 2,
                "SND_E_TR_Sequence": 4,
                "SND_E_TR_Switch": 8,
                "SND_E_TR_Random": 10,
                "SND_E_TR_Theme": 11,
            },
            "theme_disk_parts_count_offset": 36,
            "theme_disk_parts_count_read": pe.at(0x45985A, 3)
                == bytes.fromhex("8b4124"),
            "theme_disk_outro_offset": 40,
            "theme_disk_outro_read": pe.at(0x45987F, 3)
                == bytes.fromhex("8b4128"),
            "request_sound_public_address": "0x00452D20",
            "request_sound_function_prologue": pe.at(0x452D20, 4)
                == bytes.fromhex("8b442414"),
        }
        # Native 0x453BB8 and 0x453BC3 relocate INDEPENDENT
        # event/resource table offsets from the 44-byte BNM header.
        # Never infer resources start at 44 + 32*event_count.
        # Native 0x453DD6 resolves SND_EVT_Son_Play.uResource.Id.
        # The source EXE's sound-bank allocator stores the SAME numeric
        # bank ID at descriptor +4 and formats its filename using
        # literal "bnk_%d.BNM" at 0x4A252C. This closes bank ID→name
        # for this reference only; not a proof of the protected retail CD.
        result["bank_id_to_bnm_filename_reference"] = {
            "formatter": pe.name_at(0x4A252C),
            "formatter_is_bnk_decimal_bnm":
                pe.name_at(0x4A252C) == "bnk_%d.BNM",
            "bank_id_record_write": pe.at(0x453945,3)
                == bytes.fromhex("894604"),
            "bank_filename_formatter_call": pe.call_target(0x453948)
                == 0x453870,
            "formatter_literal_push": pe.at(0x45387F,5)
                == bytes.fromhex("682c254a00"),
            "bank_filename_buffer_pointer": pe.at(0x45392E,3)
                == bytes.fromhex("8d560c"),
            "reference_only_not_verified_on_original_cd": True,
        }
        result["bnm_disk_table_relocation"] = {
            "source": "ACP_Ray2/SND and native 0x453990",
            "retail_cd_bank_verified": False,
            "header_bytes": 44,
            "event_table_offset_field": 4,
            "event_count_field": 8,
            "resource_table_offset_field": 12,
            "resource_count_field": 16,
            "native_header_44_byte_read": pe.at(0x4539E4,2)
                == bytes.fromhex("6a2c"),
            "event_table_relative_offset_load": pe.at(0x453BB8,3)
                == bytes.fromhex("8b4804"),
            "event_table_relocated_from_header_base": pe.at(0x453BBB,2)
                == bytes.fromhex("03c8"),
            "resource_table_relative_offset_load": pe.at(0x453BC3,3)
                == bytes.fromhex("8b480c"),
            "resource_table_relocated_from_header_base": pe.at(0x453BC6,2)
                == bytes.fromhex("03c8"),
            "event_record_stride_32": pe.at(0x453C23,3)
                == bytes.fromhex("83c720"),
            "resource_record_stride_92": pe.at(0x453BD1,11)
                == bytes.fromhex("8d1440c1e2032bd08b410c"),
            "event_type_1_handler": f"0x{pe.dword(0x453E90):08X}",
            "event_type_1_is_play": pe.dword(0x453E90)==0x453DC9,
            "play_event_resource_id_offset_8": pe.at(0x453DC9,3)
                == bytes.fromhex("8b4608"),
            "play_event_resource_resolver": pe.call_target(0x453DD6)
                == 0x453F90,
            "play_event_resource_pointer_rewrite": pe.at(0x453DDB,3)
                == bytes.fromhex("894608"),
        }
        result["theme_controller_lifecycle"] = {
            # Check the lifecycle guard. The reference normally sets the
            # singleton active DWORD to 1 only after a nested table slot
            # has been created, and clears it on stop/initialization.
            # These are identified *direct* writes, not an exhaustive
            # proof that no indirect memory writes exist.
            "singleton_active_dword": "0x004B95B8",
            "nested_table_slot_dword": "0x004B95E8",
            "initializes_active_zero": pe.at(0x4597EC, 5)
                == bytes.fromhex("a3b8954b00"),
            "nested_slot_minus_one_guard": pe.at(0x459B06, 6)
                == bytes.fromhex("3905e8954b00"),
            "sets_active_one_after_success": pe.at(0x459B0E, 10)
                == bytes.fromhex("c705b8954b0001000000"),
            "stops_clear_active_zero": pe.at(0x459C0D, 6)
                == bytes.fromhex("c70600000000"),
            "pause_requires_active_nonzero": pe.at(0x459C44, 3)
                == bytes.fromhex("833800"),
            "resume_requires_active_nonzero": pe.at(0x459C64, 3)
                == bytes.fromhex("833800"),
            "does_not_establish_esc_runtime_state": True,
        }
        # The actual resource's nested IDs are
        # resolved through a two-stage loaded-group registry. This map
        # verifies instruction bytes, not a soundtrack identifier.
        # 0x458530 has its OWN dispatch table, distinct from
        # both 0x457E60 event creation and transport type 1/2.
        # This handles the nested type-11 event's child descriptor.
        conversion_tags = list(pe.at(0x4585A0, 11))
        conversion_targets = [pe.dword(0x45858C + 4 * i)
                              for i in range(5)]
        result["sound_event_conversion"] = {
            "function": "0x00458530",
            "resource_types_1_to_11": conversion_tags,
            "dispatch_targets": [f"0x{x:08X}" for x in conversion_targets],
            "type11_is_passthrough": (
                conversion_tags[10] == 3 and
                conversion_targets[3] == 0x458589
            ),
            "type8_nested_conversion": (
                conversion_tags[7] == 1 and
                conversion_targets[1] == 0x458564 and
                pe.call_target(0x45856F) == 0x45AA00
            ),
            "type10_nested_conversion": (
                conversion_tags[9] == 2 and
                conversion_targets[2] == 0x458550 and
                pe.call_target(0x45855B) == 0x45A4A0
            ),
            "transport_type_is_separate":
                pe.at(0x458236, 3) == bytes.fromhex("8b4604"),
        }
        # Exact disassembly signatures for the two native pause origins:
        # script 426/427 and Win32 activation call the SAME wrappers.
        # The record's +0x1C mark is written BEFORE its audio-driver
        # pause call. A naive bypass leaves that mark untouched and
        # can block focus-loss pausing of the same Theme stream.
        result["native_audio_pause_invariants"] = {
            "win32_pause_wrapper": pe.call_target(0x401C9C) == 0x4536D0,
            "script_pause_wrapper": pe.call_target(0x471E70) == 0x4536D0,
            "win32_resume_wrapper": pe.call_target(0x401CE9) == 0x453700,
            "script_resume_wrapper": pe.call_target(0x471E67) == 0x453700,
            "wrapper_pause_global": pe.call_target(0x4536ED) == 0x4580B0,
            "wrapper_resume_global": pe.call_target(0x45371D) == 0x458110,
            "pause_table_slots_64": pe.at(0x4580B7, 5)
                == bytes.fromhex("bf40000000"),
            "pause_record_mark_guard": pe.at(0x4580CA, 3)
                == bytes.fromhex("833e00"),
            "mark_written_before_dispatch": pe.at(0x4580D4, 2)
                == bytes.fromhex("8906"),
            "transition_pause_dispatch": pe.call_target(0x4580E1)
                == 0x4584A0,
            "other_resource_pause_dispatch": pe.call_target(0x4580EF)
                == 0x458070,
            "global_pause_depth_inc": pe.at(0x458100, 1)
                == bytes.fromhex("40"),
            "global_pause_depth_store": pe.at(0x458102, 5)
                == bytes.fromhex("a3c83c4a00"),
            "global_resume_depth_dec": pe.at(0x458116, 1)
                == bytes.fromhex("48"),
            "global_resume_depth_store": pe.at(0x458118, 5)
                == bytes.fromhex("a3c83c4a00"),
            "resume_record_mark_cmp": pe.at(0x45813C, 4)
                == bytes.fromhex("3bc87f24"),
            "clear_mark_before_resume": pe.at(0x458143, 6)
                == bytes.fromhex("c70600000000"),
            "transition_resume_dispatch": pe.call_target(0x458151)
                == 0x458470,
            "other_resource_resume_dispatch": pe.call_target(0x45815F)
                == 0x458090,
            "research_inference":
                "Dropping only a driver pause cannot guarantee focus-loss silence",
            "implemented_audio_bypass": False,
        }
        # Resource groups are introduced by *command-stream data*,
        # not by assuming a CD or MUSIC.DAT index. Two original command
        # tokens 0x5D/0x5E feed the same group registry preload path.
        result["resource_group_registration"] = {
            "command_class_5d_or_5e":
                pe.at(0x4507D5, 6) == bytes.fromhex("83e85d741048"),
            "single_group_register": pe.call_target(0x4507E3)
                == 0x454050,
            "listed_group_register": pe.call_target(0x450811)
                == 0x454050,
            "group_register_source": "command-stream",
            "archive_identity_still_unproven": True,
        }
        result["resource_handle_resolver"] = {
            "function": "0x00453F90",
            "group_high16": pe.at(0x453F9C, 3)
                == bytes.fromhex("c1e810"),
            "index_low16": pe.at(0x453FA0, 6)
                == bytes.fromhex("81e6ffff0000"),
            "sound_bank_registry_lookup": pe.call_target(0x453FA6)
                == 0x4538C0,
            "registry_head_load": pe.at(0x4538C0, 5)
                == bytes.fromhex("a1504d5100"),
            "bank_id_equals_group": pe.at(0x4538CD, 3)
                == bytes.fromhex("394804"),
            "bank_next_link": pe.at(0x4538D2, 3)
                == bytes.fromhex("8b403c"),
            "record_base_plus24": pe.at(0x453FB2, 3)
                == bytes.fromhex("8b4024"),
            "index_bounds_check": pe.at(0x453FB5, 3)
                == bytes.fromhex("3b7010"),
            "record_data_plus0c": pe.at(0x453FBA, 3)
                == bytes.fromhex("8b500c"),
            "index_stride_92": (
                pe.at(0x453FBD, 11)
                == bytes.fromhex("8d0c76c1e1032bce8d048a")
            ),
            "type11_nested_resolve_first": pe.call_target(0x459869)
                == 0x453F90,
            "type11_nested_resolve_second": pe.call_target(0x459890)
                == 0x453F90,
            "type11_nested_stride24": pe.at(0x459874, 3)
                == bytes.fromhex("83c618"),
            "example_decode_not_actual_music_id":
                decode_resource_handle(0x00170002),
        }
        result["archive_table"] = {
            f"disc_{disc}": {
                "SOUNDS": pe.name_at(pe.dword(0x4A2090 + (8 * disc + 4) * 4)),
                "MUSIC": pe.name_at(pe.dword(0x4A2090 + (8 * disc + 5) * 4)),
            } for disc in range(3)
        }
        # This reference's .extra2 is a file-path manifest, NOT game
        # sound data and NOT part of the approved retail CD image.
        if result["known_reference_only"]:
            extra = next(
                (entry for entry in pe.sections if entry[0] == ".extra2"),
                None,
            )
            if extra is not None:
                _, _, _, file_size, file_offset = extra
                directory = pe.read(file_offset, file_size)
                result["reference_extra2_file_manifest"] = {
                    "source": ".extra2 reference-only file-path directory",
                    **summarize_reference_file_directory(directory),
                    **summarize_gog_sound_manifest(directory),
                }
    except ValueError as exc:
        # Known VA map can differ across editions: never invent matches.
        result["address_map_failure"] = str(exc)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "executable", type=Path, help="PE32/x86 executable (read-only)"
    )
    args = parser.parse_args()
    print(json.dumps(inspect(args.executable), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
