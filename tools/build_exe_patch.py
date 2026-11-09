#!/usr/bin/env python3
"""Generate or verify a SHA256-locked delta between two user-owned Windows executables.

This tool contains no Rayman game code. The input executables are never
distributed by CI. A generated .r2patch is a candidate only; before publishing
one, inspect its literal payload for copyrighted game content.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

MAGIC = b"R2FXP01\0"
HEADER = struct.Struct("<8sQQ32s32s")
COPY = struct.Struct("<cQI")
DATA = struct.Struct("<cI")
WINDOW = 16
STEP = 8
MIN_MATCH = 32
MAX_CANDIDATES = 6
MAX_INPUT = 64 * 1024 * 1024
MAX_LITERAL_PAYLOAD = 64 * 1024
ZBLOCK = struct.Struct("<cII")

def encode_lz4_length(out: bytearray, value: int) -> None:
    while value >= 255:
        out.append(255)
        value -= 255
    out.append(value)

def lz4_compress(value: bytes) -> bytes:
    """Small, pure-Python LZ4-block encoder; no third-party dependencies."""
    out = bytearray()
    seen: dict[bytes, int] = {}
    anchor = 0
    pos = 0
    end = len(value)
    while pos + 4 <= end:
        key = value[pos:pos + 4]
        previous = seen.get(key)
        seen[key] = pos
        if previous is None or pos - previous > 65535:
            pos += 1
            continue
        length = 4
        while pos + length < end and value[previous + length] == value[pos + length]:
            length += 1
        literals = pos - anchor
        match = length - 4
        out.append((min(15, literals) << 4) | min(15, match))
        if literals >= 15:
            encode_lz4_length(out, literals - 15)
        out.extend(value[anchor:pos])
        out.extend(struct.pack("<H", pos - previous))
        if match >= 15:
            encode_lz4_length(out, match - 15)
        for offset in range(pos + 1, min(end - 3, pos + length)):
            seen[value[offset:offset + 4]] = offset
        pos += length
        anchor = pos
    literals = end - anchor
    out.append(min(15, literals) << 4)
    if literals >= 15:
        encode_lz4_length(out, literals - 15)
    out.extend(value[anchor:])
    return bytes(out)

def lz4_decompress(value: bytes, expected: int) -> bytes:
    """Test decoder for the C++ executable's Z command."""
    result = bytearray()
    pos = 0
    while pos < len(value):
        token = value[pos]
        pos += 1
        length = token >> 4
        if length == 15:
            while True:
                if pos >= len(value):
                    raise ValueError("truncated LZ4 literal extension")
                n = value[pos]
                pos += 1
                length += n
                if n != 255:
                    break
        if length > len(value) - pos or length > expected - len(result):
            raise ValueError("invalid LZ4 literal length")
        result.extend(value[pos:pos + length])
        pos += length
        if pos == len(value):
            break
        if len(value) - pos < 2:
            raise ValueError("truncated LZ4 match offset")
        offset = struct.unpack_from("<H", value, pos)[0]
        pos += 2
        if not offset or offset > len(result):
            raise ValueError("invalid LZ4 offset")
        length = 4 + (token & 15)
        if token & 15 == 15:
            while True:
                if pos >= len(value):
                    raise ValueError("truncated LZ4 match extension")
                n = value[pos]
                pos += 1
                length += n
                if n != 255:
                    break
        if length > expected - len(result):
            raise ValueError("invalid LZ4 decoded size")
        for _ in range(length):
            result.append(result[-offset])
    if len(result) != expected:
        raise ValueError("incorrect LZ4 output size")
    return bytes(result)



def sha(value: bytes) -> bytes:
    return hashlib.sha256(value).digest()


def pe32_executable(data: bytes) -> bool:
    if len(data) < 256 or data[:2] != b"MZ":
        return False
    pointer = struct.unpack_from("<I", data, 0x3C)[0]
    return (pointer + 26 <= len(data)
            and data[pointer:pointer + 4] == b"PE\0\0"
            and struct.unpack_from("<H", data, pointer + 4)[0] == 0x14C
            and struct.unpack_from("<H", data, pointer + 24)[0] == 0x10B)


def read_exe(path: Path) -> bytes:
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_INPUT:
        raise ValueError(f"unsupported or missing executable: {path}")
    data = path.read_bytes()
    if not pe32_executable(data):
        raise ValueError(f"expected a valid Windows PE32 x86 executable: {path}")
    return data


def generate_patch(source: bytes, target: bytes) -> tuple[bytes, int, int]:
    """Copy from original where possible; explicitly include only unmatched bytes."""
    index: dict[bytes, list[int]] = {}
    for p in range(0, max(0, len(source) - WINDOW + 1), STEP):
        key = source[p:p + WINDOW]
        offsets = index.setdefault(key, [])
        if len(offsets) < MAX_CANDIDATES:
            offsets.append(p)

    commands = bytearray()
    literals = bytearray()
    copied = 0
    literal_bytes = 0

    def flush_literals() -> None:
        nonlocal literal_bytes
        if not literals:
            return
        payload = bytes(literals)
        encoded = lz4_compress(payload)
        if lz4_decompress(encoded, len(payload)) != payload:
            raise ValueError("internal LZ4 mismatch")
        if len(encoded) + 4 < len(payload):
            commands.extend(ZBLOCK.pack(b"Z", len(payload), len(encoded)))
            commands.extend(encoded)
            literal_bytes += len(encoded)
        else:
            commands.extend(DATA.pack(b"L", len(payload)))
            commands.extend(payload)
            literal_bytes += len(payload)
        literals.clear()

    pos = 0
    while pos < len(target):
        best_start = -1
        best_len = 0
        if pos + WINDOW <= len(target):
            for offset in index.get(target[pos:pos + WINDOW], []):
                length = WINDOW
                while (pos + length < len(target)
                       and offset + length < len(source)
                       and target[pos + length] == source[offset + length]):
                    length += 1
                if length > best_len:
                    best_start, best_len = offset, length
        if best_len >= MIN_MATCH:
            flush_literals()
            commands.extend(COPY.pack(b"C", best_start, best_len))
            copied += best_len
            pos += best_len
        else:
            literals.append(target[pos])
            pos += 1
    flush_literals()
    commands.extend(b"E")
    header = HEADER.pack(MAGIC, len(source), len(target), sha(source), sha(target))
    return header + bytes(commands), copied, literal_bytes


def apply_patch(source: bytes, patch: bytes) -> bytes:
    if len(patch) < HEADER.size + 1:
        raise ValueError("truncated patch")
    magic, src_len, dst_len, src_hash, dst_hash = HEADER.unpack_from(patch)
    if magic != MAGIC:
        raise ValueError("invalid Rayman2Fix binary patch magic")
    if src_len != len(source) or src_hash != sha(source):
        raise ValueError("original executable SHA256 mismatch; refusing patch")
    if dst_len > MAX_INPUT:
        raise ValueError("patch output is unreasonably large")
    result = bytearray()
    pos = HEADER.size
    while pos < len(patch):
        tag = patch[pos:pos + 1]
        pos += 1
        if tag == b"E":
            if pos != len(patch):
                raise ValueError("patch has trailing data")
            break
        if tag == b"C":
            if pos + 12 > len(patch):
                raise ValueError("truncated copy operation")
            offset, length = struct.unpack_from("<QI", patch, pos)
            pos += 12
            if length == 0 or offset + length > len(source):
                raise ValueError("unsafe copy operation")
            result.extend(source[offset:offset + length])
        elif tag in (b"L", b"Z"):
            if pos + 4 > len(patch):
                raise ValueError("truncated literal length")
            (length,) = struct.unpack_from("<I", patch, pos)
            pos += 4
            encoded_length = length
            if tag == b"Z":
                if pos + 4 > len(patch):
                    raise ValueError("truncated compressed length")
                (encoded_length,) = struct.unpack_from("<I", patch, pos)
                pos += 4
            if not length or not encoded_length or pos + encoded_length > len(patch):
                raise ValueError("truncated literal operation")
            payload = patch[pos:pos + encoded_length]
            result.extend(lz4_decompress(payload, length) if tag == b"Z" else payload)
            pos += encoded_length
        else:
            raise ValueError("unknown patch command")
        if len(result) > dst_len:
            raise ValueError("patch output exceeds expected file size")
    if tag != b"E" or len(result) != dst_len or sha(result) != dst_hash:
        raise ValueError("patch output SHA256 mismatch")
    return bytes(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path, help="original Rayman2.exe extracted from the CD")
    parser.add_argument("unprotected", type=Path, help="user-supplied verified unprotected Rayman2.exe")
    parser.add_argument("output", type=Path, help="output candidate .r2patch file")
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError("refusing to overwrite an existing patch file")
        original = read_exe(args.original)
        target = read_exe(args.unprotected)
        if original == target:
            raise ValueError("source and target executables are identical")
        patch, copied, literals = generate_patch(original, target)
        if apply_patch(original, patch) != target:
            raise ValueError("self-check failed: patch does not reconstruct target")
        # Do not accidentally publish a near-complete copy of copyrighted game code.
        # Such an output can still be used locally, but the installer cannot ship it.
        print(f"Source size: {len(original)}; SHA256: {sha(original).hex()}")
        print(f"Target size: {len(target)}; SHA256: {sha(target).hex()}")
        print(f"Copied from original: {copied} bytes")
        print(f"Patch encoded unmatched payload: {literals} bytes ({literals * 100 / len(target):.1f}% of target)")
        print(f"Patch file size: {len(patch)} bytes")
        if literals > MAX_LITERAL_PAYLOAD:
            print("REFUSED: patch contains >64 KiB of encoded unmatched content; "
                  "review copyright constraints before distributing any delta.",
                  file=sys.stderr)
            return 2
        args.output.write_bytes(patch)
        print(f"Candidate patch written: {args.output}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"Rayman2Fix patch generator: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
