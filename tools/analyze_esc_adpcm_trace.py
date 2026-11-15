#!/usr/bin/env python3
"""Read-only analysis of standard Rayman2Fix ESC ADPCM diagnostics.

Only uses source-verified gate, post-dispatch voice, Theme watcher and
paired-lifetime events. Never infers audible output or stable stream identity.
Does not open, load, or change the game's DLLs or EXE. Python stdlib only.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

KV = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z_0-9]*)=([^\s()]+)")


def fields(line: str) -> dict[str, str]:
    return dict(KV.findall(line))


def integer(data: dict[str, str], name: str, default: int = -1) -> int:
    value = data.get(name)
    if value is None:
        return default
    try:
        return int(value, 10)
    except ValueError:
        return default


def key(data: dict[str, str], prefix: str) -> tuple[int, int] | None:
    bank, index = integer(data, prefix+"_bank"), integer(data, prefix+"_index")
    return (bank, index) if bank >= 0 and index >= 0 else None


def analyze(content: str) -> dict:
    opens: list[dict] = []
    watcher: list[dict] = []
    anomalies: list[str] = []
    active: dict | None = None
    pending_resume: dict | None = None
    last_lifetime: dict | None = None

    for lineno, line in enumerate(content.splitlines(), 1):
        f = fields(line)
        if line.startswith("RUNTIME-ESC-THEME-WATCH:"):
            if integer(f, "t_ms") >= 0:
                watcher.append({
                    "ms": integer(f, "t_ms"),
                    "menu": integer(f, "menu"),
                    "ref_class": integer(f, "ref_class"),
                    "outer": key(f, "outer"),
                    "alternate": key(f, "alt"),
                    "inner": key(f, "inner"),
                })
            continue

        if line.startswith("RUNTIME-ESC-PREDRIVER-GATE:"):
            t = integer(f, "t_ms")
            if t < 0:
                anomalies.append(f"line {lineno}: gate missing t_ms")
                continue
            if active is not None:
                active["overlap"]=True
                anomalies.append(f"line {lineno}: new opening before confirmed prior close")
            entry = {
                "open_ms": t,
                "source_pair_valid": (
                    integer(f, "callbacks")==2 and
                    integer(f, "transition_pauses")==2 and
                    integer(f, "other_slots")==0 and
                    integer(f, "different_handles")==0 and
                    f.get("slots","").startswith("2,2,")
                ),
                "ref_class": integer(f,"native_ref_class"),
                "ref_match": integer(f,"native_ref_match"),
                "outer": key(f,"outer"),
                "inner": key(f,"inner"),
                "transition_type": integer(f,"transition_type_post_pause"),
                "depth_before": integer(f,"depth_before"),
                "depth_after": integer(f,"depth_after"),
                "voice": None, "verdict": None, "resume": None,
                "paired_lifetime": False, "overlap": False,
                "phase6": False, "sxd_lifetime": None,
            }
            opens.append(entry)
            active=entry
            pending_resume=None
            last_lifetime=None
            continue

        if line.startswith("RUNTIME-ESC-ADPCM-VOICE:"):
            t=integer(f,"t_ms")
            matching=[entry for entry in opens if entry["open_ms"]==t]
            if len(matching)!=1 or matching[0]["voice"] is not None:
                anomalies.append(f"line {lineno}: unmatched or duplicate voice snapshot")
                continue
            matching[0]["voice"]={
                "sampled": integer(f,"sampled"),
                "ordinal": integer(f,"voice_index"),
                "paused": integer(f,"paused"),
                "other_gate": integer(f,"other_gate"),
                "transition_type": integer(f,"transition_type"),
                "post_dispatch": f.get("snapshot")=="post-dispatch",
            }
            continue

        if "RUNTIME-ESC-MUSIC: event=menu-verdict " in line:
            if active is None:
                anomalies.append(f"line {lineno}: menu verdict without opening")
            else:
                active["verdict"]=f.get("detail")
            continue

        if "RUNTIME-ESC-MUSIC: event=driver-command-rejected phase=6 " in line:
            if active is not None:
                active["phase6"]=True
            else:
                anomalies.append(f"line {lineno}: phase=6 without open source pair")
            continue

        if line.startswith("RUNTIME-ESC-PREDRIVER-RESUME:"):
            if active is None or pending_resume is not None:
                anomalies.append(f"line {lineno}: unmatched or overlapping Resume")
                pending_resume=None
                continue
            active["resume"]={
                "ms": integer(f,"t_ms"),
                "source_pair_valid": (
                    integer(f,"callbacks")==2 and
                    integer(f,"transition_resumes")==2 and
                    integer(f,"other_slots")==0 and
                    integer(f,"different_handles")==0 and
                    f.get("slots","").startswith("3,3,")
                ),
                "native_forwarded": integer(f,"native_forwarded"),
                "native_suppressed": integer(f,"native_suppressed"),
                "depth_before": integer(f,"depth_before"),
                "depth_after": integer(f,"depth_after"),
            }
            pending_resume=active
            continue

        if line.startswith("RUNTIME-ESC-PREDRIVER-LIFETIME:"):
            if pending_resume is None:
                anomalies.append(f"line {lineno}: lifetime without source Resume")
                continue
            entry=pending_resume
            lifetime_valid=(integer(f,"paired")==1 and
                            integer(f,"overlap")==0)
            entry["paired_lifetime"]=bool(
                lifetime_valid and entry["source_pair_valid"]
                and entry["resume"]["source_pair_valid"]
                and entry["resume"]["ms"]>=entry["open_ms"]
            )
            if entry["paired_lifetime"]:
                active=None
            else:
                entry["overlap"]=True
            last_lifetime=entry
            pending_resume=None
            continue

        if line.startswith("RUNTIME-ESC-SXD-LIFETIME:"):
            if last_lifetime is None:
                anomalies.append(f"line {lineno}: SXD lifetime without paired source lifetime")
                continue
            last_lifetime["sxd_lifetime"]={
                "paired": integer(f,"paired")==1,
                "first": f.get("first"),
                "last": f.get("last"),
                "changes": integer(f,"changes"),
                "samples": integer(f,"samples"),
            }
            continue

    result=[]
    for entry in opens:
        prior_theme_reference=False
        if entry["ref_class"]==3:
            earlier_alternates=[
                item["alternate"] for item in watcher
                if item["ms"]<=entry["open_ms"] and
                item["ref_class"]==3 and item["alternate"] is not None
            ]
            for alt in earlier_alternates:
                if any(
                    item["ref_class"]==1 and item["outer"]==alt and
                    item["ms"]<=entry["open_ms"]
                    for item in watcher
                ):
                    prior_theme_reference=True
                    break
        voice=entry["voice"]
        valid_voice=bool(
            entry["source_pair_valid"] and voice is not None and
            entry["transition_type"]==4 and
            voice["transition_type"]==4 and
            voice["sampled"]==1 and voice["post_dispatch"]
        )
        entry["verified_post_dispatch_voice"]=valid_voice
        entry["alternate_ref_was_previously_active"]=prior_theme_reference
        entry["rejected_paused_eligible"]=bool(
            entry["verdict"]=="live-theme-ref-not-verified" and
            entry["ref_class"]==3 and entry["ref_match"]==0 and
            valid_voice and voice["paused"]==1 and voice["other_gate"]==0
        )
        entry["compensated_unpaused_eligible"]=bool(
            entry["verdict"]=="paired-native-resume-requested" and
            entry["ref_class"]==1 and entry["ref_match"]==1 and
            valid_voice and voice["paused"]==0 and voice["other_gate"]==0
        )
        entry["confirmed_complete"]=bool(
            entry["paired_lifetime"] and not entry["overlap"]
        )
        result.append(entry)

    return {
        "schema": "esc-adpcm-source-post-dispatch-v1",
        "open_count": len(opens),
        "confirmed_complete_count": sum(x["confirmed_complete"] for x in result),
        "unconfirmed_close_count": sum(not x["confirmed_complete"] for x in result),
        "rejected_paused_count": sum(x["rejected_paused_eligible"] for x in result),
        "compensated_unpaused_count": sum(x["compensated_unpaused_eligible"] for x in result),
        "phase6_open_count": sum(x["phase6"] for x in result),
        "prior_theme_ref_count": sum(x["alternate_ref_was_previously_active"] for x in result),
        "observations": result,
        "anomalies": anomalies,
        "limits": [
            "The same voice ordinal is NOT a persistent stream/track identity.",
            "No source-verified Resume means no confirmed close.",
            "A post-dispatch voice state never authorizes pre-driver Pause suppression.",
            "Theme/SXD progression and zero Play/Stop counts do not prove physical-driver continuity.",
            "No audible outcome can be inferred from a runtime log alone.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("runtime_log",type=Path)
    p.add_argument("--json",action="store_true")
    args=p.parse_args(argv)
    try:
        summary=analyze(args.runtime_log.read_text(encoding="utf-8",errors="replace"))
    except OSError as e:
        print(f"Cannot read log: {e}",file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(summary,indent=2,ensure_ascii=False))
    else:
        print(f"ESC openings: {summary['open_count']}; paired closes: "
              f"{summary['confirmed_complete_count']}; "
              f"unconfirmed: {summary['unconfirmed_close_count']}")
        print(f"Native ref mismatch + actual paused ADPCM voice: "
              f"{summary['rejected_paused_count']}")
        print(f"Verified Theme + compensated unpaused ADPCM voice: "
              f"{summary['compensated_unpaused_count']}")
        print(f"Phase-6 safety quarantines: {summary['phase6_open_count']}")
        for x in summary["observations"]:
            v=x["voice"] or {}
            print(f"  t={x['open_ms']}ms ref_class={x['ref_class']} "
                  f"inner={x['inner']} sampled={v.get('sampled',-1)} "
                  f"paused={v.get('paused',-1)} "
                  f"verdict={x['verdict']} "
                  f"confirmed_close={int(x['confirmed_complete'])} "
                  f"phase6={int(x['phase6'])}")
        print("Caveat: source-paired state, not physical music stream identity.")
    return 0


if __name__=="__main__":
    sys.exit(main())
