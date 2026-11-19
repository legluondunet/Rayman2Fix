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
# Keyword and condition reference VAs are only 0x30 bytes apart.
# Keep samples inside the nearest *candidate* entrypoint interval; never
# treat their spans as verified function bounds.
# Directly examined in the owner's approved, full-SHA-pinned retail CD EXE.
# SHA256 of 32-byte entry windows; no commercial instruction bytes shipped.
APPROVED_MACHINE_CODE = {
    "eval_keyword": (0x480800, "06b557bc9b653073cc98265528bac45ab613235ed00e24dc5a90352c6d410ff7"),
    "eval_condition": (0x480830, "872da6ebfd32c76a5c4b2529607f08f54fd7d2ed1bef74ef3611b4e908fb0539"),
    "eval_tree": (0x480B90, "da65e3383273899b2abc7bf2498bbc5298977e6f689c8da0b4cbdfa83d34951d"),
    "keyword_resolver": (0x487D50, "523ae9b5979dd7ad3ad7139ddb094abde9e9cd58df804ff04cda92673d7356f0"),
    "keyword_shared_handler": (0x489250, "42cc9c7fd1f36c4f4c8868045aa862a19c283fcce61928f1aa272771247c0edd"),
    "eval_function": (0x480880, "ec60b75b3b555dff687987f1cdb658320e1011ef33e009eb91f6002896979433"),
    "function_resolver": (0x487820, "7c6838185370da34fc57e230a63322f338c2b1f9fa1ec6b7e793acd93dbbb838"),
    "F248_handler": (0x47A8E0, "19d2d8deee6fc7a9e98b7740d92fe9ebb280852e2188b66e5a0afc9a94023239"),
    "keyword_skip_cache": (0x489190, "68aca120dde9407959d36b609aa3569de69b2fe828260bfdf22d87311c12c2af"),
}
APPROVED_KEYWORD_TABLE_FILE_OFFSET = 0xA5E80  # approved .data mapping
APPROVED_SHARED_IF_HANDLER = 0x489250
APPROVED_IFNOT_INVERSION_VA = 0x4892DF
APPROVED_F248_TABLE_FILE_OFFSET = 0xA5C20
APPROVED_F248_HANDLER = 0x47A8E0
APPROVED_IFNOT_SHA256_16 = "10d54295e422dc0e0006ceb7b22d94bb91b7ce5094dc7d9724117318c6de16cc"

WINDOW = 32
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

    # Raw byte-pattern search ONLY: E8 + signed rel32 may occur in the
    # middle of another instruction, embedded data or jump tables.
    # These results are NOT decoded x86 callsites and cannot verify the ABI.
    patterns = {name: [] for name in REFERENCE_AI_FUNCTIONS}
    target_to_name = {va: name for name, va in REFERENCE_AI_FUNCTIONS.items()}
    for section_name, section_rva, virtual_size, raw_offset, raw_size, flags in sections:
        if not (flags & 0x20000000):
            continue
        scan_size = min(raw_size, virtual_size)
        if scan_size < 5:
            continue
        # Process the exact on-disk executable section only.
        for i in range(scan_size - 4):
            at = raw_offset + i
            if data[at] != 0xE8:
                continue
            displacement = struct.unpack_from("<i", data, at + 1)[0]
            source_va = IMAGE_BASE + section_rva + i
            destination_va = (source_va + 5 + displacement) & 0xFFFFFFFF
            name = target_to_name.get(destination_va)
            if name is not None:
                patterns[name].append(source_va - IMAGE_BASE)

    # Only the EXACT owner-approved full-file SHA may claim that its
    # dispatcher and shared If/IfNot handler were statically identified.
    # This is never approval for a detour, patch or runtime call.
    confirmed_cd = (len(data) == APPROVED_SIZE and
                    hashlib.sha256(data).hexdigest() == APPROVED_SHA256)
    if confirmed_cd:
        for name, (va, expected_digest) in APPROVED_MACHINE_CODE.items():
            if hashlib.sha256(read_exec(va, 32)).hexdigest() != expected_digest:
                raise PreflightError("approved-code-fingerprint-mismatch-" + name)
        if hashlib.sha256(read_exec(APPROVED_IFNOT_INVERSION_VA, 16)).hexdigest() != APPROVED_IFNOT_SHA256_16:
            raise PreflightError("approved-IFNOT-handler-mismatch")
        # Image and section layout already authenticated by the full hash.
        k0,k1 = struct.unpack_from("<II", data, APPROVED_KEYWORD_TABLE_FILE_OFFSET)
        if (k0,k1) != (APPROVED_SHARED_IF_HANDLER, APPROVED_SHARED_IF_HANDLER):
            raise PreflightError("approved-If-IfNot-dispatch-table-mismatch")
        (f248_handler,) = struct.unpack_from("<I", data, APPROVED_F248_TABLE_FILE_OFFSET)
        if f248_handler != APPROVED_F248_HANDLER:
            raise PreflightError("approved-F248-function-dispatch-mismatch")

    candidates = {}
    for name, va in REFERENCE_AI_FUNCTIONS.items():
        sample = read_exec(va, WINDOW)
        candidates[name] = {
            "reference_va": f"0x{va:08X}",
            "rva": f"0x{va - IMAGE_BASE:06X}",
            "sample_size": WINDOW,
            "sample_sha256": hashlib.sha256(sample).hexdigest(),
            "machine_code_reviewed": confirmed_cd,
            "call_abi_verified": False,
            # Heuristic byte patterns, not a disassembly or proven callers.
            "raw_e8_rel32_pattern_count": len(patterns[name]),
            "raw_e8_rel32_site_rvas": [
                f"0x{rva:06X}" for rva in patterns[name][:20]
            ],
            "patterns_are_verified_calls": False,
        }
    if len({x["sample_sha256"] for x in candidates.values()}) != len(candidates):
        raise PreflightError("identical-evaluator-candidates")

    return {
        "status": ("DISPATCHER_STATICALLY_IDENTIFIED_NO_HOOK" if confirmed_cd
                   else "CANDIDATE_NOT_VERIFIED"),
        "image_base": f"0x{image_base:08X}",
        "pe_timestamp": f"0x{timestamp:08X}",
        "known_control_anchors": anchors,
        "candidate_functions": candidates,
        "approved_cd_static_disassembly": confirmed_cd,
        "If_IfNot_share_same_native_handler": confirmed_cd,
        "F248_function_dispatch_verified": confirmed_cd,
        "F248_native_handler_va": ("0x0047A8E0" if confirmed_cd else None),
        "shared_handler_va": ("0x00489250" if confirmed_cd else None),
        "cdecl_three_argument_shape_observed": confirmed_cd,
        "runtime_detour_abi_or_reentrancy_proven": False,
        "writes_to_game": False,
        "allows_hook": False,
        "reason": ("keyword dispatcher, shared handler and inversion confirmed statically; scoped runtime detour remains unverified"
                   if confirmed_cd else "candidate bytes and ABI not independently verified on this fixture"),
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
