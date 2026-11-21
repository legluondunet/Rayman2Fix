#!/usr/bin/env python3
"""Read-only Rayman 2 PC SNA inventory; never writes decoded game content.

Reference algorithm: szymski/Rayman2Lib, d_tools/rayman2lib/source/decoder.d
and byvar/raymap, Assets/Scripts/OpenSpace/IO/Reader.cs.
No game assets are included or emitted. For game files owned by the user.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

_MAGIC = bytes.fromhex("79ccb56a")
_INITIAL_MASK = 0x6AB5CC79
_MASK_XOR = 0x075BD924
_MASK_DIVISOR = 0x1F31D
_MOD32 = 0xFFFFFFFF
_MAX_FILE_BYTES = 256 * 1024 * 1024
_MAX_BLOCKS = 2048
_NAME_RE = re.compile(rb"[A-Za-z][A-Za-z0-9_\\]{4,79}\x00")
_CANDIDATE_RE = re.compile(
    r"(?i)(menu|curseur|camera|reso|option|video|graph|driver|display)"
)


class SnaFormatError(ValueError):
    """Invalid/unsupported Rayman 2 encrypted SNA structure."""


def decode_sna(source: bytes) -> bytes:
    """Decode the PC Rayman 2 byte mask, preserving the 4-byte magic."""
    if len(source) < 4 or len(source) > _MAX_FILE_BYTES:
        raise SnaFormatError("SNA is empty, truncated or exceeds safety cap")
    if source[:4] != _MAGIC:
        raise SnaFormatError("unsupported SNA magic / edition")
    result = bytearray(source)
    mask = _INITIAL_MASK
    for offset in range(4, len(result)):
        result[offset] ^= (mask >> 8) & 0xFF
        intermediate = mask ^ _MASK_XOR
        mask = (
            16807 * intermediate
            - 0x7FFFFFFF * (intermediate // _MASK_DIVISOR)
        ) & _MOD32
    return bytes(result)


def inventory_blocks(decoded: bytes) -> list[dict[str, int]]:
    if decoded[:4] != _MAGIC:
        raise SnaFormatError("decoded SNA missing expected header")
    blocks = []
    offset = 4
    while offset < len(decoded):
        if len(blocks) >= _MAX_BLOCKS:
            raise SnaFormatError("excessive SNA block count")
        if len(decoded) - offset < 7:
            raise SnaFormatError("truncated SNA block prefix")
        module, block_id, unknown = struct.unpack_from("<BBB", decoded, offset)
        base_in_memory = struct.unpack_from("<i", decoded, offset + 3)[0]
        header_bytes = 7
        size = 0
        if base_in_memory != -1:
            if len(decoded) - offset < 23:
                raise SnaFormatError("truncated SNA block header")
            _, _, _, size = struct.unpack_from("<IIII", decoded, offset + 7)
            header_bytes = 23
        if size > len(decoded) - offset - header_bytes:
            raise SnaFormatError("block size exceeds remaining SNA data")
        blocks.append({
            "module": module,
            "id": block_id,
            "variant": unknown,
            "base_in_memory": base_in_memory,
            "header_offset": offset,
            "data_offset": offset + header_bytes,
            "data_size": size,
        })
        offset += header_bytes + size
    if offset != len(decoded) or not blocks:
        raise SnaFormatError("incomplete SNA block traversal")
    return blocks


def inspect_sna(source: bytes) -> dict:
    decoded = decode_sna(source)
    blocks = inventory_blocks(decoded)
    names = []
    seen = set()
    for block in blocks:
        if not block["data_size"]:
            continue
        # The original menu actor name table resides in module 4, while
        # module 13 includes waypoint labels. No generic code decompilation.
        if block["module"] not in (4, 13):
            continue
        start = block["data_offset"]
        data = decoded[start:start + block["data_size"]]
        for match in _NAME_RE.finditer(data):
            name = match.group()[:-1].decode("ascii")
            if not _CANDIDATE_RE.search(name) or name in seen:
                continue
            seen.add(name)
            names.append({
                "name": name,
                "module": block["module"],
                "decoded_offset": start + match.start(),
            })
    return {
        "format": "Rayman 2 PC masked SNA (read-only inventory)",
        "size_bytes": len(source),
        "sha256": hashlib.sha256(source).hexdigest(),
        "magic": source[:4].hex(),
        "block_count": len(blocks),
        "nonempty_block_count": sum(b["data_size"] != 0 for b in blocks),
        "blocks": blocks,
        "menu_related_name_candidates": names,
        "resolution_submenu_identified": False,
        "notes": [
            "Actor names do not prove that an Options > Resolution submenu has been found.",
            "Text labels, localization and script targets require separate verification.",
            "No original game resource or decrypted block is copied into the report.",
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", type=Path, help="locally owned Rayman 2 PC .sna file")
    args = ap.parse_args()
    try:
        report = inspect_sna(args.path.read_bytes())
    except (SnaFormatError, OSError) as exc:
        ap.exit(2, f"SNA read-only inspection failed: {exc}\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
