#!/usr/bin/env python3
"""Read-only PE32 audit of an owner-supplied Rayman 2 ADPCM DLL.

No DLL is executed, loaded, patched or copied. Uses only Python's stdlib.
Observed original Windows runtime delegates:
    StopTransitionSxdADPCM   -> APMMXbvr.dll + 0x54c0
    PauseTransitionSxdADPCM  -> APMMXbvr.dll + 0x54d0
    ResumeTransitionSxdADPCM -> APMMXbvr.dll + 0x54e0
Addresses are a per-install reference, not a universal DLL signature.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

EXPECTED_EXPORTS = {
    "_SND_fn_vStopTransitionSxdADPCM@4": 0x54C0,
    "_SND_fn_vPauseTransitionSxdADPCM@4": 0x54D0,
    "_SND_fn_vResumeTransitionSxdADPCM@4": 0x54E0,
}


# Only the original user-supplied 1999 DLL with this full-file hash
# supports the following source-independent byte-level conclusions.
# All other PE files remain structural export-only results.
OWNER_ORIGINAL_SHA256 = "ab2d7cc613fea6dd8b039e76f00a6f5d15c7ed37f2b93e0be7a86dbc6a0ee4cf"
OWNER_STATIC_SIGNATURES = {
    # Three stdcall one-argument wrappers into the original ADPCM internals.
    0x54C0: bytes.fromhex("8b 44 24 04 50 e8 76 de ff ff 83 c4 04 c2 04 00"),
    0x54D0: bytes.fromhex("8b 44 24 04 50 e8 a6 de ff ff 83 c4 04 c2 04 00"),
    0x54E0: bytes.fromhex("8b 44 24 04 50 e8 d6 de ff ff 83 c4 04 c2 04 00"),
    # Valid ADPCM record: Pause sets +0x1c to 1, Resume to 0.
    0x1EF0: bytes.fromhex("c7 46 1c 01 00 00 00"),
    0x1F30: bytes.fromhex("c7 46 1c 00 00 00 00"),
    # Mixer starts at per-record cursor +0x2c and tests +0x1c.
    0x1967: bytes.fromhex("be 04 68 0e 10"),
    0x196C: bytes.fromhex("8b 46 f0 85 c0 0f 85 ca 01 00 00"),
    0x1938: bytes.fromhex("f3 ab"),  # zeroing the mix accumulator
    0x1B4B: bytes.fromhex("81 c6 e4 00 00 00"),  # +0xe4 record stride
    # Original Stop invalidates the handle so stale Resume is unsafe.
    0x1655: bytes.fromhex("c7 06 00 00 00 00"),
}

class PEFormatError(ValueError):
    """Malformed or unsupported PE layout; never guess missing bytes."""


class PE32:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x100 or data[:2] != b"MZ":
            raise PEFormatError("not a DOS/PE image")
        pe_offset = self._u32(0x3C)
        if pe_offset < 0x40 or pe_offset + 24 > len(data):
            raise PEFormatError("PE header outside file")
        if data[pe_offset:pe_offset+4] != b"PE\0\0":
            raise PEFormatError("invalid PE signature")
        coff = pe_offset+4
        self.machine, n_sections, self.timestamp = struct.unpack_from("<HHI", data, coff)
        if self.machine != 0x14C:
            raise PEFormatError(f"expected x86 PE32, machine=0x{self.machine:04x}")
        if not 1 <= n_sections <= 96:
            raise PEFormatError("invalid number of sections")
        optional_size = self._u16(coff+16)
        optional = coff+20
        if optional_size < 104 or optional+optional_size > len(data):
            raise PEFormatError("truncated PE32 optional header")
        if self._u16(optional) != 0x10B:
            raise PEFormatError("PE32 optional magic is missing")
        self.image_base = self._u32(optional+28)
        self.image_size = self._u32(optional+56)
        self.headers_size = self._u32(optional+60)
        self.export_rva, self.export_size = struct.unpack_from("<II", data, optional+96)
        if not self.image_size or self.image_size > (1 << 31):
            raise PEFormatError("invalid mapped image size")
        self.sections = []
        section_table = optional+optional_size
        if section_table+40*n_sections > len(data):
            raise PEFormatError("section table outside file")
        for i in range(n_sections):
            offset = section_table+40*i
            name = data[offset:offset+8].split(b"\0", 1)[0].decode("ascii", errors="replace")
            virtual_size, virtual_rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset+8)
            if raw_size and (raw_offset > len(data) or raw_size > len(data)-raw_offset):
                raise PEFormatError(f"{name}: section raw data outside file")
            if virtual_rva >= self.image_size or virtual_size > self.image_size:
                raise PEFormatError(f"{name}: section image bounds invalid")
            self.sections.append((name, virtual_rva, virtual_size, raw_offset, raw_size))

    def _checked(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data)-offset:
            raise PEFormatError("out-of-range file read")
        return self.data[offset:offset+size]

    def _u16(self, offset: int) -> int:
        return struct.unpack("<H", self._checked(offset, 2))[0]

    def _u32(self, offset: int) -> int:
        return struct.unpack("<I", self._checked(offset, 4))[0]

    def rva_to_offset(self, rva: int, size: int = 1) -> int:
        if rva < 0 or size < 0 or rva > self.image_size or size > self.image_size-rva:
            raise PEFormatError(f"RVA 0x{rva:x} outside mapped image")
        if rva < self.headers_size and size <= self.headers_size-rva:
            self._checked(rva, size)
            return rva
        for _, virtual_rva, _, raw_offset, raw_size in self.sections:
            if virtual_rva <= rva and size <= raw_size and rva-virtual_rva <= raw_size-size:
                address = raw_offset+(rva-virtual_rva)
                self._checked(address, size)
                return address
        raise PEFormatError(f"RVA 0x{rva:x} is not backed by on-disk bytes")

    def read_rva(self, rva: int, size: int) -> bytes:
        return self._checked(self.rva_to_offset(rva, size), size)

    def c_string(self, rva: int, limit: int = 256) -> str:
        chars = bytearray()
        for i in range(limit):
            b = self.read_rva(rva+i, 1)[0]
            if b == 0:
                try:
                    return chars.decode("ascii")
                except UnicodeDecodeError as e:
                    raise PEFormatError("non-ASCII export name") from e
            chars.append(b)
        raise PEFormatError("unterminated export name")

    def exports(self) -> dict[str, int]:
        if self.export_rva == 0 or self.export_size < 40:
            raise PEFormatError("missing PE export table")
        ed = self.read_rva(self.export_rva, 40)
        (_, _, _, _, _, _, count_functions, count_names,
         functions_rva, names_rva, ordinals_rva) = struct.unpack("<IIHHIIIIIII", ed)
        if not 1 <= count_functions <= 65536 or not 1 <= count_names <= 65536:
            raise PEFormatError("invalid export counts")
        self.read_rva(functions_rva, count_functions*4)
        self.read_rva(names_rva, count_names*4)
        self.read_rva(ordinals_rva, count_names*2)
        out: dict[str, int] = {}
        for i in range(count_names):
            name_rva = struct.unpack("<I", self.read_rva(names_rva+i*4, 4))[0]
            ordinal = struct.unpack("<H", self.read_rva(ordinals_rva+i*2, 2))[0]
            if ordinal >= count_functions:
                raise PEFormatError("export ordinal outside function table")
            function_rva = struct.unpack("<I", self.read_rva(functions_rva+ordinal*4, 4))[0]
            name = self.c_string(name_rva)
            if name in out:
                raise PEFormatError(f"duplicate export name {name!r}")
            out[name] = function_rva
        return out


def audit_bytes(data: bytes, *, expected_sha256: str | None = None) -> dict:
    digest = hashlib.sha256(data).hexdigest()
    if expected_sha256 and expected_sha256.lower() != digest:
        raise PEFormatError("SHA-256 mismatch: this is not the selected reference DLL")
    pe = PE32(data)
    exports = pe.exports()
    owner_exact_hash = digest == OWNER_ORIGINAL_SHA256
    if owner_exact_hash:
        for rva, opcode in OWNER_STATIC_SIGNATURES.items():
            if pe.read_rva(rva, len(opcode)) != opcode:
                raise PEFormatError(
                    f"original owner DLL byte signature mismatch at RVA 0x{rva:x}"
                )
    functions = []
    good = True
    for name, required_rva in EXPECTED_EXPORTS.items():
        actual = exports.get(name)
        forwarded = actual is not None and pe.export_rva <= actual < pe.export_rva+pe.export_size
        if actual is not None and not forwarded:
            try:
                preview = pe.read_rva(actual, 24).hex(" ")
            except PEFormatError:
                preview = None
        else:
            preview = None
        matches = actual == required_rva and not forwarded and preview is not None
        good &= matches
        functions.append({
            "name": name, "observed_rva": actual,
            "expected_runtime_rva": required_rva,
            "forwarded_export": forwarded,
            "first_24_bytes": preview, "matches_runtime": matches,
        })
    return {
        "file_bytes": len(data), "sha256": digest, "format": "PE32 x86",
        "pe_timestamp_hex": f"0x{pe.timestamp:08x}",
        "pe_timestamp_utc": datetime.fromtimestamp(pe.timestamp, timezone.utc).isoformat(),
        "image_base": pe.image_base, "image_size": pe.image_size,
        "export_count": len(exports), "matches_owner_runtime": good,
        "owner_original_sha256_matches": owner_exact_hash,
        "owner_static_pause_contract": (
            {
                "adpcm_pause_flag_offset": "0x1c",
                "pause_value": 1,
                "resume_value": 0,
                "mixer_skips_paused_record": True,
                "record_stride_bytes": 228,
                "stop_invalidates_handle": True,
                "warning": "Do not infer physical voice ownership, global pause depth or safe Pause suppression.",
            } if owner_exact_hash else None
        ),
        "functions": functions,
        "evidence_limit": "Export/RVA identity only; function behavior and pause state are NOT inferred.",
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dll", type=Path, help="the owner's original Rayman2Fix/dll/APMmxBVR.dll")
    ap.add_argument("--expected-sha256", default=None,
                    help="optional full-file SHA-256 guard for a previously verified edition")
    ap.add_argument("--json", action="store_true", help="emit the same read-only facts as JSON")
    ns = ap.parse_args(argv)
    try:
        if ns.expected_sha256 and (len(ns.expected_sha256) != 64 or
                                    any(c not in "0123456789abcdefABCDEF" for c in ns.expected_sha256)):
            raise PEFormatError("--expected-sha256 must be 64 hexadecimal characters")
        raw = ns.dll.read_bytes()
        report = audit_bytes(raw, expected_sha256=ns.expected_sha256)
    except (OSError, PEFormatError, OverflowError) as e:
        print(f"APMMX audit: {e}", file=sys.stderr)
        return 2
    if ns.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Original binary: {ns.dll.name} ({report['file_bytes']} bytes)")
        print(f"SHA-256: {report['sha256']}")
        if report["owner_original_sha256_matches"]:
            print("Owner's exact original 1999 ADPCM DLL: opcode signatures verified")
            print("Per-record pause flag +0x1c: Pause=1, Resume=0; mixer skips paused records")
            print("Stop invalidates the original handle; deferred Resume cannot be presumed safe")
        else:
            print("Owner's exact 1999 hash does not match: exports only, no pause-contract claims")
        print(f"PE32 x86: timestamp={report['pe_timestamp_utc']} image_base=0x{report['image_base']:08x} image_size=0x{report['image_size']:x}")
        for item in report["functions"]:
            rva = item["observed_rva"]
            val = "missing" if rva is None else f"0x{rva:08x}"
            status = "MATCH" if item["matches_runtime"] else "UNVERIFIED"
            print(f"  {item['name']}: {val}, owner runtime 0x{item['expected_runtime_rva']:04x}: {status}")
            if item["first_24_bytes"]:
                print(f"    first 24 bytes: {item['first_24_bytes']}")
        print("Static byte evidence only; does not authorize any native Pause suppression.")
    return 0 if report["matches_owner_runtime"] else 1


if __name__ == "__main__":
    sys.exit(main())
