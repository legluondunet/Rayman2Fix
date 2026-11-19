#!/usr/bin/env python3
"""Read-only PE32 interpreter preflight for the approved Rayman 2 PC executable.

NEVER patches or launches the game. Exact SHA-256 is required for CLI use.
Reference AI evaluator addresses are only hypotheses, not hook authorization.
No bytes from the commercial executable are copied into generated output.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import sys

APPROVED_SIZE = 1468928
APPROVED_SHA256 = "9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713"
APPROVED_PE_TIMESTAMP = 0x3807B68B
IMAGE_BASE = 0x400000

# Reference VAs in raytools/ACP_Ray2/src/AI/AI.c, *not* verified hook sites.
REFERENCE_AI_FUNCTIONS = {
    "eval_keyword": 0x480800,
    "eval_condition": 0x480830,
    "eval_tree": 0x480B90,
}
# Two independently approved CD code anchors already used by Rayman2Fix.
# kNativeScreenToVertexRva and kSuperimposedProcRva in runtime.cpp.
KNOWN_ANCHORS = {
    "screen_to_vertex": (0x422940, bytes.fromhex("81 EC D4 00 00 00")),
    "superimposed_proc": (0x4710C0, bytes.fromhex("83 EC 28 8D 44 24 30")),
}
WINDOW = 64
MAX_FILE_SIZE = 64 * 1024 * 1024


class PreflightError(ValueError):
    pass


def _take(buf: bytes, offset: int, length: int) -> bytes:
    if offset < 0 or length < 0 or offset > len(buf) or length > len(buf) - offset:
        raise PreflightError("truncated-or-out-of-bounds-PE")
    return buf[offset:offset + length]


def _u16(buf: bytes, offset: int) -> int:
    return struct.unpack("<H", _take(buf, offset, 2))[0]


def _u32(buf: bytes, offset: int) -> int:
    return struct.unpack("<I", _take(buf, offset, 4))[0]


def inspect_pe32(data: bytes) -> dict:
    """Parse trusted/untrusted fixture bytes read-only; never authorize a hook.

    The CLI separately requires the EXACT approved file hash and length.
    This function can be unit-tested with generated synthetic PE fixtures.
    """
    if len(data) < 256 or len(data) > MAX_FILE_SIZE:
        raise PreflightError("invalid-file-size")
    if _take(data, 0, 2) != b"MZ":
        raise PreflightError("not-MZ")
    pe = _u32(data, 0x3C)
    if pe < 0x40 or pe > 1024 * 1024 or _take(data, pe, 4) != b"PE\x00\x00":
        raise PreflightError("not-PE")
    if _u16(data, pe + 4) != 0x014C:  # IMAGE_FILE_MACHINE_I386
        raise PreflightError("not-x86")
    section_count = _u16(data, pe + 6)
    optional_size = _u16(data, pe + 20)
    opt = pe + 24
    if section_count < 1 or section_count > 24 or optional_size < 96:
        raise PreflightError("bad-section-or-optional-header-count")
    _take(data, opt, optional_size)
    if _u16(data, opt) != 0x10B:
        raise PreflightError("not-PE32")
    image_base = _u32(data, opt + 28)
    image_size = _u32(data, opt + 56)
    timestamp = _u32(data, pe + 8)
    if image_base != IMAGE_BASE or not 0x80000 < image_size < 0x10000000:
        raise PreflightError("unexpected-image-layout")
    if timestamp != APPROVED_PE_TIMESTAMP:
        raise PreflightError("unexpected-PE-timestamp")

    sections = []
    section_header = opt + optional_size
    _take(data, section_header, 40 * section_count)
    for i in range(section_count):
        sh = section_header + 40 * i
        name = _take(data, sh, 8).rstrip(b"\x00").decode("ascii", "replace")
        virtual_size = _u32(data, sh + 8)
        rva = _u32(data, sh + 12)
        raw_size = _u32(data, sh + 16)
        raw_offset = _u32(data, sh + 20)
        flags = _u32(data, sh + 36)
        # Reject wrapped/overflowing sections and raw spans that run off EOF.
        if rva > image_size or max(virtual_size, raw_size) > image_size - rva:
            raise PreflightError("section-overruns-image")
        if raw_offset > len(data) or raw_size > len(data) - raw_offset:
            raise PreflightError("section-overruns-file")
        sections.append((name, rva, virtual_size, raw_offset, raw_size, flags))

    def read_exec(va: int, count: int) -> bytes:
        rva = va - IMAGE_BASE
        if rva < 0 or rva >= image_size or count > image_size - rva:
            raise PreflightError("address-outside-image")
        match = []
        for name, start, virt, raw_offset, raw_size, flags in sections:
            if not (flags & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
                continue
            # Never report virtual zero-fill or data from another section.
            if (rva >= start and rva - start <= raw_size and
                    count <= raw_size - (rva - start) and
                    rva - start < max(raw_size, virt)):
                match.append((name, raw_offset + (rva - start)))
        if len(match) != 1:
            raise PreflightError("candidate-does-not-map-to-exactly-one-executable-section")
        return _take(data, match[0][1], count)

    anchors = {}
    for name, (va, expected) in KNOWN_ANCHORS.items():
        if read_exec(va, len(expected)) != expected:
            raise PreflightError("approved-anchor-mismatch-" + name)
        anchors[name] = "matched"

    candidates = {}
    for name, va in REFERENCE_AI_FUNCTIONS.items():
        sample = read_exec(va, WINDOW)
        candidates[name] = {
            "reference_va": f"0x{va:08X}",
            "rva": f"0x{va - IMAGE_BASE:06X}",
            "sample_size": WINDOW,
            "sample_sha256": hashlib.sha256(sample).hexdigest(),
            "machine_code_reviewed": False,
            "call_abi_verified": False,
        }
    if len({x["sample_sha256"] for x in candidates.values()}) != len(candidates):
        raise PreflightError("identical-evaluator-candidates")

    return {
        "status": "CANDIDATE_NOT_VERIFIED",
        "image_base": f"0x{image_base:08X}",
        "pe_timestamp": f"0x{timestamp:08X}",
        "known_control_anchors": anchors,
        "candidate_functions": candidates,
        "writes_to_game": False,
        "allows_hook": False,
        "reason": "code bytes and ABI of these evaluator entries still need independent verification",
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("Usage: python3 learn10_interpreter_preflight.py /path/to/approved/unprotected/rayman2.exe",
              file=sys.stderr)
        return 2
    candidate = Path(argv[1])
    try:
        if candidate.is_symlink() or not candidate.is_file():
            raise PreflightError("not-a-regular-non-symlink-file")
        size = candidate.stat().st_size
        if size != APPROVED_SIZE:
            raise PreflightError("unapproved-file-length")
        # Hash the exact bytes, not the filename, timestamp or directory.
        data = candidate.read_bytes()
        if len(data) != APPROVED_SIZE or hashlib.sha256(data).hexdigest() != APPROVED_SHA256:
            raise PreflightError("unapproved-file-sha256")
        report = inspect_pe32(data)
        report["approved_full_file_sha256"] = APPROVED_SHA256
    except (OSError, PreflightError) as exc:
        print(json.dumps({"status": "REJECTED", "reason": str(exc),
                          "allows_hook": False, "writes_to_game": False},
                         sort_keys=True))
        return 2
    print(json.dumps(report, indent=2, sort_keys=True))
    # Deliberately "incomplete": verified file identity and anchor mapping
    # do not establish original semantics or the correct detour ABI.
    return 3


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
