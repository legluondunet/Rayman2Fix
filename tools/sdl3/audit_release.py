#!/usr/bin/env python3
"""Audit the pinned SDL3 Windows x86 release without executing or unpacking it."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile

import pefile

VERSION = "3.4.20"
ARCHIVE = f"SDL3-{VERSION}-win32-x86.zip"
ARCHIVE_SHA256 = "f6fbf5afa42195a01b1da8adf25567377d0e212f4b2b6770923903eb3feea254"
URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{VERSION}/{ARCHIVE}"
REQUIRED_EXPORTS = {
    "SDL_GetVersion", "SDL_GetError", "SDL_SetMainReady", "SDL_InitSubSystem",
    "SDL_QuitSubSystem", "SDL_SetHint", "SDL_GetGamepads", "SDL_free",
    "SDL_OpenGamepad", "SDL_CloseGamepad", "SDL_GamepadConnected",
    "SDL_GetGamepadID", "SDL_GetGamepadName", "SDL_GetGamepadType", "SDL_GetGamepadAxis",
    "SDL_GetGamepadButton", "SDL_SetGamepadEventsEnabled", "SDL_UpdateGamepads",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def select(z, names, limit):
    matches = [i for i in z.infolist() if not i.is_dir()
               and PurePosixPath(i.filename.replace("\\", "/")).name.lower() in names]
    if len(matches) != 1 or not 0 < matches[0].file_size <= limit:
        raise ValueError(f"expected exactly one bounded archive member for {names}")
    return z.read(matches[0])


def audit(archive):
    if digest(archive) != ARCHIVE_SHA256:
        raise ValueError("SDL3 archive SHA256 mismatch")
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        dll = select(z, {"sdl3.dll"}, 32 * 1024 * 1024)
        license_data = select(z, {"license", "license.txt"}, 128 * 1024)
    license_text = license_data.decode("utf-8-sig").lower()
    if ("as-is" not in license_text or "altered source versions" not in license_text
            or "this notice may not be removed" not in license_text):
        raise ValueError("SDL3 zlib notice not recognized")
    pe = pefile.PE(data=dll, fast_load=True)
    try:
        if (pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.Magic != 0x10b
                or not pe.FILE_HEADER.Characteristics & 0x2000):
            raise ValueError("SDL3.dll is not a PE32 i386 DLL")
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        exports = {s.name.decode("ascii") for s in pe.DIRECTORY_ENTRY_EXPORT.symbols
                   if s.name and not s.forwarder}
        missing = REQUIRED_EXPORTS - exports
        if missing:
            raise ValueError(f"missing SDL3 exports: {sorted(missing)}")
    finally:
        pe.close()
    return {"version": VERSION, "archive_url": URL, "archive_sha256": digest(archive),
            "dll_sha256": digest(dll), "license_sha256": digest(license_data),
            "dll_size": len(dll), "architecture": "PE32-i386",
            "required_exports": sorted(REQUIRED_EXPORTS)}, dll, license_data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive", type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    if args.archive.stat().st_size > 16 * 1024 * 1024:
        p.error("archive exceeds expected size bound")
    manifest, dll, notice = audit(args.archive.read_bytes())
    # Never extract arbitrary ZIP paths; write only fixed names after all checks.
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "SDL3.dll").write_bytes(dll)
    (args.output / "SDL3-LICENSE.txt").write_bytes(notice)
    (args.output / "sdl3-audit.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
