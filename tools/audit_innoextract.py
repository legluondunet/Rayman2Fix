#!/usr/bin/env python3
"""CI-only acquisition and inventory of the pinned official extractor.

No game installer is downloaded or executed here. The publisher's SHA256SUMS
pins the ZIP; this is unrelated to any GOG package fingerprint.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import stat
import struct
import subprocess
import sys
import urllib.request
import zipfile

URL = "https://constexpr.org/innoextract/files/innoextract-1.9/innoextract-1.9-windows.zip"
SHA256 = "6989342c9b026a00a72a38f23b62a8e6a22cc5de69805cf47d68ac2fec993065"
LIMIT = 8 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    with urllib.request.urlopen(URL, timeout=60) as response:
        blob = response.read(LIMIT + 1)
    if len(blob) > LIMIT or sha(blob) != SHA256:
        raise ValueError("unexpected official innoextract archive digest")
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        files = {}
        names = set()
        total = 0
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            mode = entry.external_attr >> 16
            if (path.is_absolute() or ".." in path.parts or
                    "\\" in entry.filename or ":" in entry.filename or
                    stat.S_ISLNK(mode)):
                raise ValueError("unsafe dependency ZIP entry")
            if entry.is_dir():
                continue
            name = path.name.lower()
            if name in names:
                raise ValueError("ambiguous dependency ZIP entry")
            names.add(name)
            total += entry.file_size
            if total > LIMIT:
                raise ValueError("dependency ZIP expands beyond limit")
            files[name] = archive.read(entry)
    binary = files.get("innoextract.exe")
    if binary is None or len(binary) < 64 or binary[:2] != b"MZ":
        raise ValueError("official x86 binary missing")
    pe = struct.unpack_from("<I", binary, 60)[0]
    if pe > len(binary) - 26 or binary[pe:pe+4] != b"PE\0\0":
        raise ValueError("invalid PE")
    if struct.unpack_from("<H", binary, pe+4)[0] != 0x14c or struct.unpack_from("<H", binary, pe+24)[0] != 0x10b:
        raise ValueError("dependency is not PE32 i386")
    if not any("license" in name or "copying" in name for name in files):
        raise ValueError("upstream license notice missing")
    output.mkdir(parents=True, exist_ok=False)
    # Preserve all upstream notices and files for dependency review.
    for name, data in files.items():
        (output / name).write_bytes(data)
    manifest = {name: {"size": len(data), "sha256": sha(data)}
                for name, data in sorted(files.items())}
    print(json.dumps(manifest, indent=2), flush=True)
    (output / "inventory.json").write_text(json.dumps(manifest, indent=2)+"\n",
                                          encoding="utf-8")
    if sys.platform == "win32":
        exe = str(output / "innoextract.exe")
        result = subprocess.run([exe, "--version"], cwd=output, timeout=30,
                                capture_output=True, text=True, check=True)
        print(result.stdout)
        if "innoextract 1.9" not in result.stdout:
            raise ValueError("unexpected extractor version")
    return 0


if __name__ == "__main__":
    sys.exit(main())
