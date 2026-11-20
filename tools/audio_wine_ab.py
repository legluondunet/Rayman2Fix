#!/usr/bin/env python3
"""Compare one Rayman2Fix Wine session with/without a PulseAudio latency override.

No game files, Wine prefix configuration, or PipeWire settings are changed.
Optional pw-top captures graph scheduling telemetry; it is not an audio recording.
Run once per mode, with the same Wine runner, prefix and output device.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

MODES = {"baseline": None, "latency60": "60", "latency100": "100"}


def parse(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=MODES, help="One test session; run baseline before latency60")
    p.add_argument("--launcher", type=Path, default=Path("Rayman2Fix.exe"),
                   help="Path to installed Rayman2Fix.exe (default: current directory)")
    p.add_argument("--wine", default="wine", help="Same Wine executable for all test sessions")
    p.add_argument("--output", type=Path, help="Diagnostics directory (default: launcher/audio-diagnostics)")
    p.add_argument("--no-pw-top", action="store_true", help="Skip optional PipeWire telemetry")
    p.add_argument("--pw-top-iterations", type=int, default=240,
                   help="Maximum samples (1 per second, default 240)")
    args = p.parse_args(argv)
    if not 1 <= args.pw_top_iterations <= 3600:
        p.error("--pw-top-iterations must be between 1 and 3600")
    return args


def audio_environment(mode: str, incoming: dict[str, str]) -> dict[str, str]:
    env = incoming.copy()
    override = MODES[mode]
    if override is not None:
        env["PULSE_LATENCY_MSEC"] = override
    return env


def run_test(args: argparse.Namespace) -> int:
    launcher = args.launcher.expanduser().resolve()
    if not launcher.is_file():
        raise ValueError(f"Launcher not found: {launcher}")
    if not shutil.which(args.wine):
        raise ValueError(f"Wine runner not found: {args.wine}")
    output_dir = (args.output or (launcher.parent / "audio-diagnostics")).expanduser().resolve()
    if output_dir == launcher.parent:
        raise ValueError("Diagnostics output must be a subdirectory, not the launcher directory")
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    prefix = output_dir / f"{timestamp}-{args.mode}"
    meta_path = prefix.with_suffix(".json")
    pwtop_path = prefix.with_suffix(".pw-top.txt")

    env = audio_environment(args.mode, os.environ)
    info: dict[str, object] = {
        "schema": "rayman2fix-audio-ab-v1",
        "mode": args.mode,
        "start_local": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "launcher_name": launcher.name,
        "requested_pulse_latency_ms": env.get("PULSE_LATENCY_MSEC"),
        "preexisting_pipewire_latency": os.environ.get("PIPEWIRE_LATENCY"),
        "pwtop_captured": False,
        "game_process_exit_code": None,
        "elapsed_seconds": None,
        "note": "Does not diagnose xrun alone; pw-top ERR is graph-level evidence only.",
    }
    if args.mode == "baseline" and "PULSE_LATENCY_MSEC" in os.environ:
        print("NOTE: baseline inherits an existing PULSE_LATENCY_MSEC override from your environment.")
    if os.environ.get("PIPEWIRE_LATENCY"):
        print("NOTE: PIPEWIRE_LATENCY is already set; keep it unchanged for both sessions.")
    if not args.no_pw_top and not shutil.which("pw-top"):
        print("NOTE: pw-top unavailable; continuing without PipeWire telemetry.")

    process = None
    telemetry = None
    started = time.monotonic()
    print(f"Mode={args.mode}; requested PulseAudio latency={env.get('PULSE_LATENCY_MSEC','unchanged')}")
    print("No game or system changes. Quit Rayman2Fix normally to finish this session.")
    try:
        if not args.no_pw_top and shutil.which("pw-top"):
            telemetry = pwtop_path.open("wb")
            try:
                process = subprocess.Popen(
                    ["pw-top", "--batch-mode", "--iterations", str(args.pw_top_iterations)],
                    stdout=telemetry, stderr=subprocess.DEVNULL,
                )
                info["pwtop_captured"] = True
            except OSError as error:
                print(f"NOTE: cannot start pw-top ({error}); proceeding without telemetry.")
                telemetry.close()
                telemetry = None
        result = subprocess.run([args.wine, str(launcher)], cwd=launcher.parent,
                                env=env, check=False)
        info["game_process_exit_code"] = result.returncode
        return result.returncode
    finally:
        info["elapsed_seconds"] = round(time.monotonic() - started, 2)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if telemetry is not None:
            telemetry.close()
        if not info["pwtop_captured"] and pwtop_path.exists():
            pwtop_path.unlink()
        meta_path.write_text(json.dumps(info, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Result metadata: {meta_path}")
        if info["pwtop_captured"]:
            print(f"Read-only PipeWire telemetry: {pwtop_path}")
        print("Note audible crackles in normal gameplay, cinematics and ESC pause separately.")
        print("A nonzero pw-top ERR counter alone does not establish root cause.")


def main(argv: list[str] | None = None) -> int:
    args = parse(argv)
    try:
        return run_test(args)
    except (ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
