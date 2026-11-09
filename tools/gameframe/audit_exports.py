#!/usr/bin/env python3
"""Audit pinned GameFrame's Glide 2.x x86 DLL exports before bundling it."""
import re
import sys
from pathlib import Path
import pefile

ESSENTIAL = {
    "_grGlideInit@0", "_grGlideShutdown@0", "_grSstOpen@24",
    "_grSstQueryHardware@4", "_grBufferSwap@4",
    "_grLfbGetWritePtr@4", "_grLfbEnd@0"
}

def main(path, definitions):
    declared = {
        match.group(1)
        for line in Path(definitions).read_text(encoding="utf-8").splitlines()
        if (match := re.match(r"\s*(_[A-Za-z0-9_]+@\d+)\s*=", line))
    }
    if len(declared) != 156:
        raise ValueError(f"expected 156 Glide exports in .def, found {len(declared)}")
    pe = pefile.PE(path)
    if pe.FILE_HEADER.Machine != 0x14C or not pe.is_dll():
        raise ValueError("GameFrame DLL must be a PE32 Windows x86 DLL")
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    actual = {
        entry.name.decode("ascii")
        for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols if entry.name
    }
    missing = sorted(declared - actual)
    # Upstream intentionally exports the ordinary Rust DLL entry point
    # and a Glide 2.11 splash compatibility alias in addition to its 156
    # canonical Glide API calls; neither should be treated as an ABI error.
    expected_extras = {"DllMain", "grSplash21"}
    unknown_extra = sorted(actual - declared - expected_extras)
    print(f"GameFrame PE32 x86 Glide: expected 156 canonical exports; actual {len(actual)}")
    if missing or unknown_extra or ESSENTIAL - actual:
        raise ValueError(f"Glide export mismatch: missing={missing}, "
                         f"unexpected_extra={unknown_extra}, "
                         f"essentials_missing={ESSENTIAL - actual}")
    print("GameFrame 156 decorated Win32 Glide exports: PASS")
    print("Rayman 2 rendering compatibility: not yet validated on the game")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: audit_exports.py glide2x.dll glide2x.def")
    main(sys.argv[1], sys.argv[2])
