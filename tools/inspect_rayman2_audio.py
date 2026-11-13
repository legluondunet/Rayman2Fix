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
        result["event_type11"] = {
            "dispatch_prologue_expected": pe.at(EVENT_DISPATCH, 6)
                == bytes.fromhex("53e87af6ffff"),
            "table_entries": len(events),
            "case11_target": f"0x{events[10]:08X}",
            "case11_verified": events[10] == 0x457F21,
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
        # The actual event descriptor's nested resource IDs are
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
            "event_classes_1_to_11": conversion_tags,
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
        result["resource_handle_resolver"] = {
            "function": "0x00453F90",
            "group_high16": pe.at(0x453F9C, 3)
                == bytes.fromhex("c1e810"),
            "index_low16": pe.at(0x453FA0, 6)
                == bytes.fromhex("81e6ffff0000"),
            "group_registry_lookup": pe.call_target(0x453FA6)
                == 0x4538C0,
            "registry_head_load": pe.at(0x4538C0, 5)
                == bytes.fromhex("a1504d5100"),
            "registry_key_equals_group": pe.at(0x4538CD, 3)
                == bytes.fromhex("394804"),
            "registry_next_link": pe.at(0x4538D2, 3)
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
