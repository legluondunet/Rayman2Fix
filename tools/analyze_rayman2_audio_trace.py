#!/usr/bin/env python3
"""Read-only evidence summary for Rayman2Fix's optional Wine audio trace.

This is NOT a menu detector or a game patch. Script opcodes 0x1AA/0x1AB
pause/resume sounds generically; audio_origin=game-script-pause never proves
that the in-game ESC menu is open. Captured events may be sampling-limited.
No paths, raw driver handles, stack values or private log lines are emitted.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

FIELD = re.compile(r"(?:^|\s)([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)")
TRACE = "RUNTIME-AUDIO-TRACE: event="
OWNER = "RUNTIME-AUDIO-THEME-OWNER: channel="


def fields(line: str) -> dict[str, str]:
    return dict(FIELD.findall(line))


def number(obj: dict, key: str, default: int = -1) -> int:
    try:
        return int(obj.get(key, ""))
    except (ValueError, TypeError):
        return default


def summarize(text: str) -> dict:
    events: list[dict] = []
    by_event_sample: dict[tuple[str, int], dict] = {}
    unmatched_owners = 0
    schema2 = False
    for line in text.splitlines():
        if "RUNTIME-AUDIO-TRACE: enabled=1 schema=2" in line:
            schema2 = True
        if TRACE in line:
            data = fields(line.split("RUNTIME-AUDIO-TRACE:", 1)[1])
            event = data.get("event")
            sample = number(data, "sample")
            if not event or sample < 0:
                continue
            obj = {
                "event": event, "sample": sample,
                "ms": number(data, "t_ms"),
                "origin": data.get("audio_origin", "unknown"),
                "depth": number(data, "global_pause_depth"),
                "foreground": number(data, "foreground"),
                "esc_key_down": number(data, "esc_down"),
                "owner": None,
            }
            events.append(obj)
            by_event_sample[(event, sample)] = obj
        elif OWNER in line:
            data = fields(line.split("RUNTIME-AUDIO-THEME-OWNER:", 1)[1])
            key = (data.get("channel", ""), number(data, "sample"))
            event = by_event_sample.get(key)
            if event is None or event["owner"] is not None:
                unmatched_owners += 1
                continue
            # These are native resource IDs, not asserted musical identity.
            event["owner"] = {
                "eligible": (
                    number(data, "theme_owned_driver") == 1
                    and number(data, "outer_candidates") == 1
                    and number(data, "inner_handle_matches") == 1
                    and number(data, "inner_ref_class") == 3
                ),
                "outer": (
                    number(data, "theme_bank"),
                    number(data, "theme_resource"),
                ),
                "inner": (
                    number(data, "inner_bank"),
                    number(data, "inner_resource"),
                ),
                "inner_slot": number(data, "inner_slot"),
                "outer_slot": number(data, "outer_slot"),
            }

    qualified = [
        e for e in events if e["owner"] is not None and e["owner"]["eligible"]
    ]
    roots: dict[tuple[int, int], set[tuple[int, int]]] = defaultdict(set)
    for e in qualified:
        roots[e["owner"]["outer"]].add(e["owner"]["inner"])

    # A pair is only a close-in-time, same-root, same-part correlation.
    # The native call stacks and per-record marks are NOT part of this trace.
    pairs: list[dict] = []
    index = 0
    while index + 1 < len(qualified):
        left, right = qualified[index], qualified[index + 1]
        if (
            left["event"] == right["event"]
            and left["event"] in ("PauseTransitionSxd", "ResumeTransitionSxd")
            and left["origin"] == right["origin"]
            and left["ms"] >= 0 and 0 <= right["ms"] - left["ms"] <= 5
            and left["depth"] == right["depth"]
            and left["owner"]["outer"] == right["owner"]["outer"]
            and left["owner"]["inner"] == right["owner"]["inner"]
        ):
            pairs.append({
                "event": left["event"],
                "time_first_ms": left["ms"],
                "time_second_ms": right["ms"],
                "origin": left["origin"],
                "outer_resource": list(left["owner"]["outer"]),
                "inner_resource": list(left["owner"]["inner"]),
                "global_depth": left["depth"],
            })
            index += 2  # one pair consumes two samples
        else:
            index += 1

    stops_while_nested = [
        {
            "time_ms": e["ms"],
            "outer_resource": list(e["owner"]["outer"]),
            "inner_resource": list(e["owner"]["inner"]),
            "global_depth": e["depth"],
        }
        for e in qualified
        if e["event"] == "StopTransitionSxd" and e["depth"] > 1
    ]

    roots_out = [
        {
            "outer_resource": list(root),
            "observed_inner_resources": [list(x) for x in sorted(inner)],
            "inner_resource_changed": len(inner) > 1,
        }
        for root, inner in sorted(roots.items())
    ]
    focus_out = any(e["foreground"] == 0 for e in events)
    return {
        "schema_2_header_seen": schema2,
        "events_sampled": len(events),
        "events_by_kind": dict(sorted(Counter(e["event"] for e in events).items())),
        "origins": dict(sorted(Counter(e["origin"] for e in events).items())),
        "ownership_records_validated": len(qualified),
        "ownership_lines_unmatched": unmatched_owners,
        "close_same_driver_event_pairs": pairs,
        "theme_controller_resources_observed": roots_out,
        "owned_stops_at_nested_depth": stops_while_nested,
        "foreground_zero_seen_in_sampled_events": focus_out,
        "focus_loss_not_proven_when_missing": not focus_out,
        "script_origin_does_not_prove_ESC_menu": True,
        "esc_key_down_does_not_prove_ESC_menu": True,
        "theme_ownership_does_not_prove_audible_music": True,
        "sampled_events_do_not_prove_all_runtime_events": True,
        "no_runtime_audio_behavior_changed": True,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, help="locally supplied read-only text log")
    args = ap.parse_args()
    print(json.dumps(summarize(args.log.read_text(errors="replace")),
                     indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
