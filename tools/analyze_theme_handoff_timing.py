#!/usr/bin/env python3
"""Offline timing audit of the original Rayman 2 Theme 9:12→9:5 handoff.

The trace watcher samples changed native game state only periodically;
the computed "outside ESC menus" time is a proxy, not a driver clock,
proof of sample decoding, or authorization to bypass native Pause.
Only source-verified original paired ESC script windows are subtracted.
Unknown/overlapping/unclosed intervals FAIL CLOSED for that run.
The original ESC script also pauses/resumes ordinary sound effects:
these are expected; only the two original same-handle music transition
callbacks must be source-matched, consistent and forwarded unchanged.


The tool only reads log text; no Windows process, game DLL, or EXE is
loaded or modified. Python standard library only.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

KV=re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)=([^\s()]+)")


def fields(line: str) -> dict[str,str]:
    return dict(KV.findall(line))


def number(item: dict[str,str],key: str, default: int=-1) -> int:
    try:
        return int(item.get(key,str(default)))
    except ValueError:
        return default


def evaluate(content: str) -> dict:
    watcher=[]
    events=[]
    for lineno,line in enumerate(content.splitlines(),1):
        f=fields(line)
        t=number(f,"t_ms")
        if t<0:
            continue
        if line.startswith("RUNTIME-ESC-THEME-WATCH:"):
            if f.get("outer_bank")=="9" and f.get("outer_index") in ("5","12"):
                watcher.append({
                    "ms":t,"class":number(f,"ref_class"),
                    "index":number(f,"outer_index"),
                    "state":number(f,"controller_field0c"),
                })
        if line.startswith("RUNTIME-ESC-PREDRIVER-GATE:"):
            events.append(("open",t,f,lineno))
        elif line.startswith("RUNTIME-ESC-PREDRIVER-RESUME:"):
            events.append(("resume",t,f,lineno))
        elif line.startswith("RUNTIME-ESC-PREDRIVER-LIFETIME:"):
            events.append(("lifetime",t,f,lineno))

    handoff=next((w for w in watcher if
                  w["class"]==3 and w["index"]==5 and w["state"]==1),None)
    if not handoff:
        return {"qualified":False,"reason":"no native 9:12→9:5 handoff watcher"}
    start=handoff["ms"]
    prior=any(w["ms"]<start and w["class"]==1 and w["index"]==12
              for w in watcher)
    settlement=next((w for w in watcher if
                     w["ms"]>start and w["class"]==1 and
                     w["index"]==5 and w["state"]==0),None)
    if not settlement:
        return {"qualified":False,"reason":"no matching 9:5 settlement watcher",
                "start_ms":start}
    end=settlement["ms"]
    filtered=[event for event in events if start<=event[1]<=end]
    errors=[]
    if not prior:
        errors.append("previous 9:12 active owner was not witnessed")
    active=None
    pending=None
    intervals=[]
    for kind,t,f,lineno in filtered:
        if kind=="open":
            if active is not None or pending is not None:
                errors.append(f"line {lineno}: overlapping or unclosed prior script Pause")
            # The original script Pause also pauses ordinary sound effects.
            # Two music transition callbacks need not be consecutive:
            # owner trace has slots=2,0,0,0, other_slots=6, for example.
            if (number(f,"transition_pauses")!=2 or
                number(f,"other_slots")<0 or
                number(f,"callbacks")!=2+number(f,"other_slots") or
                number(f,"different_handles")!=0 or
                number(f,"depth_before")!=1 or number(f,"depth_after")!=2 or
                number(f,"owner_driver")!=1 or
                number(f,"native_ref_class")!=3 or
                number(f,"native_ref_match")!=0):
                errors.append(f"line {lineno}: invalid original source Pause pair")
            active=t
        elif kind=="resume":
            if active is None or pending is not None:
                errors.append(f"line {lineno}: unpaired Resume")
            else:
                if (number(f,"transition_resumes")!=2 or
                    number(f,"other_slots")<0 or
                    number(f,"callbacks")!=2+number(f,"other_slots") or
                    number(f,"different_handles")!=0 or
                    number(f,"depth_before")!=2 or number(f,"depth_after")!=1 or
                    number(f,"native_forwarded")!=2 or
                    number(f,"native_suppressed")!=0):
                    errors.append(f"line {lineno}: invalid source Resume pair")
                pending=(active,t)
                active=None
        elif kind=="lifetime":
            if pending is None:
                errors.append(f"line {lineno}: lifetime without just-closed Resume")
            else:
                if (number(f,"paired")!=1 or number(f,"overlap")!=0 or
                    number(f,"elapsed_ms")!=pending[1]-pending[0] or
                    number(f,"play_between")!=0 or number(f,"stop_between")!=0):
                    errors.append(f"line {lineno}: ambiguous source-specific lifetime")
                else:
                    intervals.append(pending)
                pending=None
    if active is not None or pending is not None:
        errors.append("script ESC opening not fully closed before 9:5 settlement")
    # The exact source code establishes Pause transitions, but the
    # trace is not sampled continuously and cannot rule out stops
    # internal to the original CD Theme/ADPCM engine.
    script_menu_ms=sum(stop-start for start,stop in intervals)
    wall_ms=end-start
    if script_menu_ms>wall_ms:
        errors.append("sum of script ESC windows exceeds entire handoff")
    return {
        "qualified":not errors,
        "reason":"qualified source-paired timing proxy" if not errors
                 else "excluded ambiguous source/Theme interval",
        "errors":errors,
        "start_ms":start,"settled_ms":end,
        "wall_handoff_ms":wall_ms,
        "verified_script_esc_intervals":len(intervals),
        "script_esc_window_ms":script_menu_ms,
        "outside_esc_ms":wall_ms-script_menu_ms if not errors else None,
        "scope":"9:12 current → 9:5 pending/current; original CD",
        "limitations":[
            "Outside-ESC time is only a proxy for audio-progress time.",
            "Change-triggered Theme watcher cadence adds sampling uncertainty.",
            "No physical ADPCM stream lifetime is established by this comparison.",
            "Source-specific native Play/Stop counters do not track all internal transitions.",
            "No synthetic audio command is authorized by this analysis.",
        ],
    }


def summarize(texts: list[tuple[str,str]]) -> dict:
    rows=[{"file":name,**evaluate(content)} for name,content in texts]
    seconds=[r["outside_esc_ms"]/1000 for r in rows if r["qualified"]]
    return {
        "run_count":len(rows),
        "qualified_count":len(seconds),
        "excluded_count":len(rows)-len(seconds),
        "outside_esc_seconds_mean":round(statistics.mean(seconds),3) if seconds else None,
        "outside_esc_seconds_median":round(statistics.median(seconds),3) if seconds else None,
        "outside_esc_seconds_range":(
            [round(min(seconds),3),round(max(seconds),3)] if seconds else None),
        "runs":rows,
    }


def main(argv: list[str] | None=None) -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs",type=Path,nargs="+")
    parser.add_argument("--json",action="store_true")
    args=parser.parse_args(argv)
    try:
        result=summarize([(str(path.name),
                           path.read_text(encoding="utf-8",errors="replace"))
                          for path in args.logs])
    except OSError as exc:
        print(f"Theme timing audit: {exc}",file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result,indent=2))
    else:
        for run in result["runs"]:
            if run["qualified"]:
                print(f"{run['file']}: handoff={run['wall_handoff_ms']} ms, "
                      f"paired ESC windows={run['script_esc_window_ms']} ms "
                      f"({run['verified_script_esc_intervals']}), "
                      f"outside-ESC={run['outside_esc_ms']} ms")
            else:
                print(f"{run['file']}: EXCLUDED ({run['reason']})")
                for error in run.get("errors",[]):print("  "+error)
        print(f"Qualified {result['qualified_count']}/{result['run_count']}; "
              f"outside-ESC mean={result['outside_esc_seconds_mean']} s, "
              f"median={result['outside_esc_seconds_median']} s")
        print("This is a timing proxy, NOT proof of per-voice clock ownership.")
    return 0


if __name__=="__main__":
    sys.exit(main())
