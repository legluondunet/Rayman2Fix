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
UI_STATE = "RUNTIME-AUDIO-UI-STATE:"
MENU_ACTOR = "RUNTIME-AUDIO-MENU-ACTOR:"


def fields(line: str) -> dict[str, str]:
    return dict(FIELD.findall(line))


def number(obj: dict, key: str, default: int = -1) -> int:
    try:
        return int(obj.get(key, ""))
    except (ValueError, TypeError):
        return default


def native_bank_location(fields_map: dict[str, str], prefix: str) -> tuple[int, int] | None:
    """Require complete, unique native loaded-bank membership evidence."""
    bank = number(fields_map, prefix + "_native_bank")
    index = number(fields_map, prefix + "_native_index")
    count = number(fields_map, prefix + "_bank_candidates")
    complete = number(fields_map, prefix + "_bank_list_complete")
    if (0 <= bank <= 0xFFFF and 0 <= index <= 0xFFFF
            and count == 1 and complete == 1):
        return bank, index
    return None


def bracket_observed_menu_flag_changes(
    observations: list[dict],
) -> tuple[list[dict], int]:
    """Consecutive 0/1 snapshots bound *observations*, not true UI events.

    A VM script may set this flag before/after a sound driver call. Polling
    and sample caps mean intermediate flips can be missed. Actual ESC menu
    visibility is not inferred from keyboard state alone. A timestamp reset within one session invalidates an
    interval; header-defined sessions never bridge.
    """
    prior_by_session: dict[int, dict] = {}
    windows: list[dict] = []
    nonmonotonic = 0
    for obs in observations:
        if obs["value"] not in (0, 1) or obs["ms"] < 0:
            # A failed or missing read breaks observability. Skipping it
            # would invent continuity between two valid snapshots.
            prior_by_session.pop(obs["session"], None)
            continue
        previous = prior_by_session.get(obs["session"])
        if previous is not None and obs["ms"] < previous["ms"]:
            nonmonotonic += 1
            prior_by_session[obs["session"]] = obs
            continue
        if previous is not None and obs["value"] != previous["value"]:
            windows.append({
                "session": obs["session"],
                "observed_change": (
                    "0-to-1" if obs["value"] == 1 else "1-to-0"),
                "earliest_bound_ms": previous["ms"],
                "latest_bound_ms": obs["ms"],
                "bounds_width_ms": obs["ms"] - previous["ms"],
                "previous_sample_source": previous["source"],
                "new_sample_source": obs["source"],
                "exact_change_time_not_observed": True,
                "intermediate_state_changes_cannot_be_excluded": True,
                "not_proof_of_ESC_or_F1_menu": True,
            })
        prior_by_session[obs["session"]] = obs
    return windows, nonmonotonic


def summarize(text: str) -> dict:
    events: list[dict] = []
    by_event_sample: dict[tuple[int, str, int], dict] = {}
    unmatched_owners = 0
    ui_states: list[dict] = []
    menu_actor_snapshots: list[dict] = []
    menu_flag_observations: list[dict] = []
    schema2 = False
    session = 0  # 0 means data before any explicit session header
    unmarked_prefix_events = 0
    for line in text.splitlines():
        if "RUNTIME-AUDIO-TRACE: enabled=1 schema=2" in line:
            schema2 = True
            session += 1
            continue
        if TRACE in line:
            data = fields(line.split("RUNTIME-AUDIO-TRACE:", 1)[1])
            event = data.get("event")
            sample = number(data, "sample")
            if not event or sample < 0:
                continue
            if session == 0:
                unmarked_prefix_events += 1
            obj = {
                "session": session,
                "event": event, "sample": sample,
                "ms": number(data, "t_ms"),
                "origin": data.get("audio_origin", "unknown"),
                "depth": number(data, "global_pause_depth"),
                "foreground": number(data, "foreground"),
                "esc_key_down": number(data, "esc_down"),
                "ui_in_game_menu_at_call": number(data, "ui_in_game_menu_at_call"),
                "ui_engine_paused_at_call": number(data, "ui_engine_paused_at_call"),
                "owner": None,
            }
            events.append(obj)
            by_event_sample[(session, event, sample)] = obj
            # Missing UI fields are -1 (including older logs). Preserve
            # that failed observation to prevent fabricated bounds.
            menu_flag_observations.append({
                "session": session,
                "ms": obj["ms"],
                "value": obj["ui_in_game_menu_at_call"],
                "source": "before-sampled-audio-driver-call",
            })
        elif MENU_ACTOR in line:
            data = fields(line.split(MENU_ACTOR, 1)[1])
            menu = number(data, "in_game_menu")
            engine = number(data, "engine_paused")
            esc = number(data, "esc_down")
            candidate = number(data, "personal_type_found")
            active = number(data, "dynamic_actor")
            inactive = number(data, "inactive_actor")
            ms = number(data, "t_ms")
            if (ms >= 0 and menu in (0, 1) and engine in (0, 1)
                    and esc in (0, 1)
                    and candidate in (-1, 0, 1)
                    and active in (-1, 0, 1)
                    and inactive in (-1, 0, 1)):
                menu_actor_snapshots.append({
                    "session": session,
                    "time_ms": ms,
                    "native_menu_flag": menu,
                    "native_engine_pause_flag": engine,
                    "esc_key_down_at_sample": esc,
                    "candidate_personal_type_registered": candidate,
                    "candidate_actor_in_active_world": active,
                    "candidate_actor_in_inactive_world": inactive,
                    "registry_nodes_checked": number(data, "type_nodes"),
                    "active_world_nodes_checked": number(data, "dynamic_nodes"),
                    "inactive_world_nodes_checked": number(data, "inactive_nodes"),
                    "name_only_candidate_not_ESC_UI_proof": True,
                    "unknown_or_missing_actor_not_proof_of_absence": True,
                })
        elif UI_STATE in line:
            data = fields(line.split(UI_STATE, 1)[1])
            menu, engine = number(data, "in_game_menu"), number(data, "engine_paused")
            if menu in (0, 1) and engine in (0, 1):
                ui_states.append({
                    "session": session,
                    "ms": number(data, "t_ms"),
                    "in_game_menu": menu,
                    "engine_paused": engine,
                    "foreground": number(data, "foreground"),
                })
                menu_flag_observations.append({
                    "session": session,
                    "ms": number(data, "t_ms"),
                    "value": menu,
                    "source": "periodic-ui-poll-change",
                })
        elif OWNER in line:
            data = fields(line.split("RUNTIME-AUDIO-THEME-OWNER:", 1)[1])
            key = (session, data.get("channel", ""), number(data, "sample"))
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
                # New research-only fields from the *loaded bank registry*;
                # previous theme_bank/inner_bank are merely halves of Id.
                "outer_native": native_bank_location(data, "outer"),
                "inner_native": native_bank_location(data, "inner"),
                "bank_provenance_fields_present": (
                    "outer_native_bank" in data and
                    "inner_native_bank" in data),
                "outer_bank_scan_complete": number(
                    data, "outer_bank_list_complete"),
                "inner_bank_scan_complete": number(
                    data, "inner_bank_list_complete"),
            }

    qualified = [
        e for e in events if e["owner"] is not None and e["owner"]["eligible"]
    ]
    roots: dict[tuple[int, tuple[int, int]], set[tuple[int, int]]] = defaultdict(set)
    for e in qualified:
        roots[(e["session"], e["owner"]["outer"])].add(e["owner"]["inner"])

    # A pair is only a close-in-time, same-root, same-part correlation.
    # The native call stacks and per-record marks are NOT part of this trace.
    pairs: list[dict] = []
    index = 0
    while index + 1 < len(qualified):
        left, right = qualified[index], qualified[index + 1]
        if (
            left["session"] == right["session"]
            and left["event"] == right["event"]
            and left["event"] in ("PauseTransitionSxd", "ResumeTransitionSxd")
            and left["origin"] == right["origin"]
            and left["ms"] >= 0 and 0 <= right["ms"] - left["ms"] <= 5
            and left["depth"] == right["depth"]
            and left["owner"]["outer"] == right["owner"]["outer"]
            and left["owner"]["inner"] == right["owner"]["inner"]
            and left["owner"]["outer_native"] == right["owner"]["outer_native"]
            and left["owner"]["inner_native"] == right["owner"]["inner_native"]
        ):
            pairs.append({
                "session": left["session"],
                "event": left["event"],
                "time_first_ms": left["ms"],
                "time_second_ms": right["ms"],
                "origin": left["origin"],
                "outer_resource": list(left["owner"]["outer"]),
                "inner_resource": list(left["owner"]["inner"]),
                "global_depth": left["depth"],
                "outer_loaded_bank_and_index": (
                    list(left["owner"]["outer_native"])
                    if left["owner"]["outer_native"] is not None else None),
                "inner_loaded_bank_and_index": (
                    list(left["owner"]["inner_native"])
                    if left["owner"]["inner_native"] is not None else None),
                "menu_state_at_calls": [
                    left["ui_in_game_menu_at_call"],
                    right["ui_in_game_menu_at_call"],
                ],
                "engine_pause_state_at_calls": [
                    left["ui_engine_paused_at_call"],
                    right["ui_engine_paused_at_call"],
                ],
            })
            index += 2  # one pair consumes two samples
        else:
            index += 1

    stops_while_nested = [
        {
            "session": e["session"],
            "time_ms": e["ms"],
            "outer_resource": list(e["owner"]["outer"]),
            "inner_resource": list(e["owner"]["inner"]),
            "outer_loaded_bank_and_index": (
                list(e["owner"]["outer_native"])
                if e["owner"]["outer_native"] is not None else None),
            "inner_loaded_bank_and_index": (
                list(e["owner"]["inner_native"])
                if e["owner"]["inner_native"] is not None else None),
            "global_depth": e["depth"],
            "native_menu_flag_at_call": e["ui_in_game_menu_at_call"],
            "engine_pause_flag_at_call": e["ui_engine_paused_at_call"],
            "foreground_at_call": e["foreground"],
            "audio_origin": e["origin"],
            "menu_flag_zero_is_not_a_resume_or_stop_reason_proof": True,
        }
        for e in qualified
        if e["event"] == "StopTransitionSxd" and e["depth"] > 1
    ]

    roots_out = [
        {
            "session": session_id,
            "outer_resource": list(root),
            "observed_inner_resources": [list(x) for x in sorted(inner)],
            "inner_resource_changed": len(inner) > 1,
            "these_are_native_record_Id_halves_not_authenticated_bank_indices": True,
        }
        for (session_id, root), inner in sorted(roots.items())
    ]
    focus_out = any(e["foreground"] == 0 for e in events)
    menu_windows, menu_nonmonotonic = bracket_observed_menu_flag_changes(
        menu_flag_observations)
    return {
        "schema_2_header_seen": schema2,
        "explicit_sessions_detected": session,
        "unmarked_prefix_events": unmarked_prefix_events,
        "native_ui_state_samples": ui_states,
        "menu_actor_readonly_snapshots": menu_actor_snapshots,
        "menu_actor_candidate_not_sufficient_to_prove_ESC_visibility": True,
        "older_logs_lacking_actor_snapshots_are_not_negative_evidence": True,
        "observed_native_menu_flag_change_windows": menu_windows,
        "unusable_nonmonotonic_menu_timestamp_pairs": menu_nonmonotonic,
        "menu_windows_are_not_confirmed_ESC_UI_lifetimes": True,
        "in_game_menu_flag_cannot_distinguish_ESC_from_F1": True,
        "events_sampled": len(events),
        "events_by_kind": dict(sorted(Counter(e["event"] for e in events).items())),
        "observed_transition_counts": {
            "pause": sum(e["event"] == "PauseTransitionSxd" for e in events),
            "resume": sum(e["event"] == "ResumeTransitionSxd" for e in events),
            "stop": sum(e["event"] == "StopTransitionSxd" for e in events),
        },
        "zero_recorded_resume_is_not_proof_of_no_native_resume": True,
        "generic_script_driver_ui_snapshots": [
            {
                "session": e["session"],
                "event": e["event"],
                "time_ms": e["ms"],
                "origin": e["origin"],
                "native_menu_flag_at_call": e["ui_in_game_menu_at_call"],
                "native_engine_pause_at_call": e["ui_engine_paused_at_call"],
                "game_menu_visibility_not_proven": True,
            }
            for e in events
            if e["origin"] in ("game-script-pause", "game-script-resume")
            and e["event"] in ("PauseTransitionSxd", "ResumeTransitionSxd")
        ],
        "origins": dict(sorted(Counter(e["origin"] for e in events).items())),
        "ownership_records_validated": len(qualified),
        "ownership_has_native_loaded_bank_provenance": sum(
            1 for e in qualified
            if e["owner"]["outer_native"] is not None
            and e["owner"]["inner_native"] is not None),
        "native_loaded_bank_provenance": [
            {
                "session": e["session"],
                "event": e["event"],
                "time_ms": e["ms"],
                "outer_bank_and_index": list(e["owner"]["outer_native"]),
                "inner_bank_and_index": list(e["owner"]["inner_native"]),
                "native_bank_lookup_is_not_audio_or_ESC_proof": True,
            }
            for e in qualified
            if e["owner"]["outer_native"] is not None
            and e["owner"]["inner_native"] is not None
        ],
        "record_Id_halves_are_not_authenticated_BNM_provenance": True,
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
