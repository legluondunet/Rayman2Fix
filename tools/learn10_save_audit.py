#!/usr/bin/env python3
"""Read-only Rayman 2 PC save inventory. Does NOT decode saved story bits."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

FORMAT = "rayman2-save-inventory-v1"
MAX_FILES = 4096
MAX_SIZE = 64 * 1024 * 1024
PATHS = (("Data", "SaveGame"), ("Menu", "SaveGame"),
         ("game", "Data", "SaveGame"), ("game", "Menu", "SaveGame"))


def save_dirs(root: Path) -> list[Path]:
    found = []
    for path in PATHS:
        p = root
        for part in path:
            if p.is_symlink() or not p.is_dir():
                break
            try:
                matched = [x for x in p.iterdir() if x.name.casefold() == part.casefold()]
            except OSError:
                break
            if len(matched) != 1 or matched[0].is_symlink() or not matched[0].is_dir():
                break
            p = matched[0]
        else:
            if p not in found:
                found.append(p)
    return found


def inventory(root: Path) -> dict:
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Root must be an existing non-symlink directory")
    root = root.resolve(strict=True)
    dirs = save_dirs(root)
    files = {}
    warnings = []
    incomplete = not bool(dirs)
    if incomplete:
        warnings.append("No known PC save directory (Data/SaveGame or Menu/SaveGame)")
    for folder in dirs:
        for base, subdirs, names in os.walk(folder, followlinks=False):
            base = Path(base)
            for name in list(subdirs):
                item = base / name
                if item.is_symlink():
                    subdirs.remove(name)
                    incomplete = True
                    warnings.append("Refused symlink dir: " + item.relative_to(root).as_posix())
            for name in names:
                path = base / name
                rel = path.relative_to(root).as_posix()
                if path.is_symlink():
                    incomplete = True
                    warnings.append("Refused symlink file: " + rel)
                    continue
                try:
                    stat = path.stat()
                    if not path.is_file() or stat.st_size > MAX_SIZE:
                        incomplete = True
                        warnings.append("Refused unknown/oversized file: " + rel)
                        continue
                    if len(files) >= MAX_FILES:
                        raise ValueError("More than 4096 files; safety limit")
                    digest = hashlib.sha256()
                    with path.open("rb") as stream:
                        for block in iter(lambda: stream.read(1024 * 1024), b""):
                            digest.update(block)
                    files[rel] = {"sha256": digest.hexdigest(),
                                  "size": stat.st_size,
                                  "mtime_ns": stat.st_mtime_ns}
                except OSError as exc:
                    incomplete = True
                    warnings.append("Cannot inspect " + rel + ": " + type(exc).__name__)
    return {"format": FORMAT, "directories": sorted(p.relative_to(root).as_posix()
             for p in dirs), "files": dict(sorted(files.items())),
            "scan_incomplete": incomplete, "warnings": warnings}


def compare(before: dict, after: dict) -> dict:
    if before.get("format") != FORMAT or after.get("format") != FORMAT \
            or not isinstance(before.get("files"), dict):
        raise ValueError("Invalid baseline format")
    old = before["files"]
    new = after["files"]
    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    changed = sorted(k for k in set(old) & set(new) if old[k] != new[k])
    incomplete = before.get("scan_incomplete") or after.get("scan_incomplete")
    status = "INCOMPLETE" if incomplete else (
        "CHANGED" if added or removed or changed else "UNCHANGED")
    return {"format": "rayman2-save-comparison-v1", "status": status,
            "added": added, "removed": removed, "changed": changed,
            "warnings": before.get("warnings", []) + after.get("warnings", []),
            "disclaimer": "Metadata/hash check ONLY. This cannot prove bit1188 absent from saved data or certify gameplay."}


def write_out(report: dict, output: Path, root: Path) -> None:
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Invalid root")
    target = output.resolve()
    if any(target == d or d in target.parents for d in save_dirs(root.resolve())):
        raise ValueError("Report must be OUTSIDE SaveGame directories")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest="cmd", required=True)
    one = commands.add_parser("snapshot")
    one.add_argument("root", type=Path)
    one.add_argument("output", type=Path)
    two = commands.add_parser("compare")
    two.add_argument("root", type=Path)
    two.add_argument("baseline", type=Path)
    two.add_argument("output", type=Path)
    opts = p.parse_args(argv)
    try:
        after = inventory(opts.root)
        if opts.cmd == "snapshot":
            report = after
            status = "INCOMPLETE" if after["scan_incomplete"] else "SNAPSHOT"
        else:
            if opts.baseline.resolve() == opts.output.resolve():
                raise ValueError("Do not overwrite baseline")
            report = compare(json.loads(opts.baseline.read_text(encoding="utf-8")), after)
            status = report["status"]
        write_out(report, opts.output, opts.root)
        print(f"{status}: {opts.output} (not a save-content or bit1188 validator)")
        return 3 if status == "INCOMPLETE" else 0
    except (ValueError, OSError, json.JSONDecodeError) as e:
        print("SAVE-AUDIT-ERROR: " + str(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
