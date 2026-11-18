#!/usr/bin/env python3
"""Audit a single Rayman2Fix learn_10 NOMOVIES prototype session.

A CANDIDATE result establishes ONLY in-memory bit restoration and a
rapid engine handover. It NEVER establishes visual correctness, save
isolation, absence of game crashes or Wine compatibility.

Standalone/offline: Python standard library only.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

PROTOTYPE = "RUNTIME-LEARN10-PROTOTYPE:"
INTRO = "RUNTIME-INTRO:"
MAX_EXPOSURE_MS = 5000
FAST_HANDOVER_CUTOFF_MS = 20000


def _first(lines: list[str], needle: str) -> tuple[int, str] | None:
    for i, line in enumerate(lines):
        if needle in line:
            return i, line
    return None


def _all(lines: list[str], needle: str) -> list[tuple[int, str]]:
    return [(i, line) for i, line in enumerate(lines) if needle in line]


def _integer(line: str, key: str) -> int | None:
    # Some native timestamps are logged as t=3125ms, while integer
    # state fields use plain values separated by whitespace.
    match = re.search(
        r"(?:^|\s)" + re.escape(key) + r"=(\d+)(?=ms(?:\s|$)|\s|$)",
        line,
    )
    return int(match.group(1)) if match else None


def audit(text: str) -> dict:
    lines = text.splitlines()
    failures: list[str] = []
    missing: list[str] = []
    warnings: list[str] = []
    sessions = _all(lines, "RUNTIME-SESSION:")
    enabled = _all(lines, PROTOTYPE + " enabled=1")
    arm = _all(lines, PROTOTYPE + " phase=armed ")
    edge = _all(lines, PROTOTYPE + " phase=physical-enter-edge ")
    restore = _all(lines, PROTOTYPE + " phase=restored ")
    pending = _all(lines, PROTOTYPE + " phase=restore-pending ")
    errors = _all(lines, PROTOTYPE + " phase=error ")
    entry = _first(lines, INTRO + " nomovies-flag at-entry ")
    exit_bit = _first(lines, INTRO + " nomovies-flag at-handover ")
    handover = _first(lines, INTRO + " natural=handover t=")
    final_director = _first(lines, INTRO + " director=handover ")
    post = _first(lines, INTRO + " natural=handover-plus-1s ")
    outcomes = _all(lines, PROTOTYPE + " phase=handover ")
    checks = _all(lines, PROTOTYPE + " phase=handover-verification ")

    if len(sessions) != 1:
        failures.append("Log must contain exactly one non-cumulative runtime session")
    if not enabled:
        return {
            "status": "NOT_APPLICABLE",
            "handover_ms": None, "armed_ms": None,
            "enter_edges": len(edge), "exposure_ms": None,
            "failures": failures,
            "missing": ["Dedicated NOMOVIES prototype not enabled"],
            "warnings": warnings,
            "visual_verified": False, "saves_verified": False,
        }
    if len(enabled) != 1 or len(arm) > 1 or len(restore) > 1:
        failures.append("Unexpected repeated prototype activation/restoration")
    if pending or errors:
        failures.append("Prototype recorded restore-pending or write/verification error")
    if not arm:
        missing.append("Original NOMOVIES flag was never armed")
    if not edge:
        missing.append("No physical Enter edge observed while flag was active")
    if not restore:
        failures.append("No verified rollback of the temporary NOMOVIES flag")

    if entry is None or _integer(entry[1], "known") != 1 or \
            _integer(entry[1], "enabled") != 0:
        failures.append("NOMOVIES entry bit not demonstrably known and initially clear")
    if exit_bit is None or _integer(exit_bit[1], "known") != 1 or \
            _integer(exit_bit[1], "enabled") != 0:
        failures.append("NOMOVIES at handover is not demonstrably clear")
    if handover is None:
        missing.append("No engine handover snapshot")
    if final_director is None or \
            _integer(final_director[1], "suspended") != 0 or \
            _integer(final_director[1], "behaviour") != 1:
        missing.append("No handover snapshot confirming director=1 and Rayman unsuspended")
    if not post:
        warnings.append("No post-handover observation at +1 second")

    armed_ms = _integer(arm[0][1], "t") if arm else None
    handover_ms = _integer(handover[1], "t") if handover else None
    exposure_ms = _integer(restore[0][1], "armed_duration_ms") if restore else None

    if arm and restore and restore[0][0] < arm[0][0]:
        failures.append("Flag restored before it was armed (invalid event order)")
    if restore and handover and restore[0][0] > handover[0]:
        failures.append("Flag rollback occurred after engine handover")
    if arm and edge and edge[0][0] <= arm[0][0]:
        failures.append("Physical Enter was detected before the flag was armed")
    if edge and restore and edge[0][0] >= restore[0][0]:
        missing.append("Physical Enter was not detected during the armed interval")
    if arm and handover and arm[0][0] >= handover[0]:
        failures.append("Flag was armed after engine handover")

    if restore and "state=already-clear" not in restore[0][1]:
        if _integer(restore[0][1], "nomovies_bit1188") != 0 or \
                "other_progress_bits=preserved" not in restore[0][1]:
            failures.append("Restoration log does not confirm isolated bit1188 rollback")
    if exposure_ms is None and restore:
        warnings.append("Legacy trace has no exact flag exposure duration")
    elif exposure_ms is not None and exposure_ms > MAX_EXPOSURE_MS:
        failures.append(
            "Flag exposure exceeded 5000ms (possible stalled render callback)"
        )

    if len(outcomes) > 1 or len(checks) > 1:
        failures.append("Duplicated handover checks")
    if outcomes:
        state = outcomes[0][1]
        if (_integer(state, "story1091") != 1 or
                _integer(state, "ever_armed") != 1 or
                _integer(state, "restored") != 1 or
                _integer(state, "still_armed") != 0):
            failures.append("Prototype handover reported incomplete rollback")
    if checks:
        if "status=pass" not in checks[0][1] or \
                _integer(checks[0][1], "known") != 1 or \
                _integer(checks[0][1], "bit1188") != 0 or \
                _integer(checks[0][1], "rollback_complete") != 1:
            failures.append("In-memory handover-verification failed")
    else:
        warnings.append("Old prototype: no explicit handover-verification verdict")

    if handover_ms is not None and handover_ms >= FAST_HANDOVER_CUTOFF_MS:
        missing.append("Handover was not fast relative to ~25s natural baseline")

    if failures:
        status = "UNSAFE"
    elif missing:
        status = "INCOMPLETE"
    else:
        status = "CANDIDATE"
    warnings.append(
        "In-memory evidence only; game visuals, controls, Wine and saved files "
        "must be validated separately"
    )
    return {
        "status": status,
        "handover_ms": handover_ms,
        "armed_ms": armed_ms,
        "enter_edges": len(edge),
        "exposure_ms": exposure_ms,
        "failures": failures, "missing": missing, "warnings": warnings,
        "visual_verified": False, "saves_verified": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Rayman2Fix runtime log")
    parser.add_argument("--json", action="store_true", help="Print JSON verdict")
    args = parser.parse_args(argv)
    try:
        report = audit(args.log.read_text(encoding="utf-8", errors="replace"))
    except OSError as exc:
        parser.error(str(exc))
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print("Learn_10 NOMOVIES audit:", report["status"])
        print("Handover:", report["handover_ms"], "ms")
        print("Armed at:", report["armed_ms"], "ms")
        print("Physical Enter edges:", report["enter_edges"])
        print("Flag exposure:", report["exposure_ms"], "ms")
        for kind in ("failures", "missing", "warnings"):
            for message in report[kind]:
                print(kind.upper() + ":", message)
    return {"CANDIDATE": 0, "UNSAFE": 2, "INCOMPLETE": 3,
            "NOT_APPLICABLE": 4}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
