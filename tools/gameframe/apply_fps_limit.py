#!/usr/bin/env python3
"""Install Rayman2Fix's small frame pacer in the PINNED Apache-2.0 GameFrame Glide build.

Modifies only the temporary third_party/game-frame checkout in GitHub CI, not
the upstream project or checked-in proprietary Rayman 2 files. Fail closed
if the upstream API changes rather than silently building without a limit.
"""
from __future__ import annotations

import pathlib
import sys

MODULE = r'''// Rayman2Fix integration for the pinned GameFrame Glide2x renderer.
// The limiter lives on the game's grBufferSwap calling thread, not the
// asynchronous OpenGL presentation thread: this prevents an ever-growing
// render queue and caps the producer instead of only slowing the display.
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

struct Clock {
    period: Duration,
    deadline: Option<Instant>,
}

fn clock() -> Option<&'static Mutex<Clock>> {
    static LIMITER: OnceLock<Option<Mutex<Clock>>> = OnceLock::new();
    LIMITER.get_or_init(|| {
        let frames = std::env::var("RAYMAN2FIX_FPS_LIMIT")
            .ok()
            .and_then(|v| v.parse::<u32>().ok())
            .unwrap_or(60);
        if frames == 0 || !(15..=240).contains(&frames) {
            return None;
        }
        Some(Mutex::new(Clock {
            period: Duration::from_secs_f64(1.0 / f64::from(frames)),
            deadline: None,
        }))
    }).as_ref()
}

pub fn pace() {
    let Some(clock) = clock() else { return };
    // A poisoned mutex may indicate a failure in another thread; do not
    // make rendering crash because the optional limiter failed.
    let Ok(mut guard) = clock.lock() else { return };
    let now = Instant::now();
    if let Some(deadline) = guard.deadline {
        // After stalls, pauses or Alt+Tab, reset the cadence rather than
        // attempting to catch up by displaying many frames at once.
        if deadline > now && deadline.duration_since(now) < guard.period {
            std::thread::sleep(deadline.duration_since(now));
            guard.deadline = Some(deadline + guard.period);
            return;
        }
    }
    guard.deadline = Some(now + guard.period);
}
'''

def install(root: pathlib.Path) -> None:
    source = root / "wrappers" / "glide" / "src"
    lib = source / "lib.rs"
    sst = source / "api" / "sst.rs"
    pace = source / "fps_limit.rs"
    if not lib.is_file() or not sst.is_file():
        raise SystemExit("Expected pinned GameFrame Glide sources are missing")
    original_lib = lib.read_text(encoding="utf-8")
    original_sst = sst.read_text(encoding="utf-8")
    lib_anchor = "#[cfg(windows)]\nmod config;"
    swap_anchor = '    push(Cmd::Swap);\n    flush();\n    super::movie::frame();'
    if original_lib.count(lib_anchor) != 1 or original_sst.count(swap_anchor) != 1:
        raise SystemExit("Pinned GameFrame layout changed: refusing to apply FPS patch")
    if "mod fps_limit;" in original_lib or "crate::fps_limit::pace();" in original_sst:
        raise SystemExit("GameFrame already has an FPS limiter; review duplicate patch")
    if pace.exists():
        raise SystemExit("GameFrame fps_limit.rs exists; refusing overwrite")
    lib.write_text(original_lib.replace(lib_anchor, lib_anchor + "\n#[cfg(windows)]\nmod fps_limit;"),
                   encoding="utf-8")
    sst.write_text(original_sst.replace(
        swap_anchor,
        '    push(Cmd::Swap);\n    flush();\n    crate::fps_limit::pace();\n    super::movie::frame();'),
        encoding="utf-8")
    pace.write_text(MODULE, encoding="utf-8")
    print("Applied Rayman2Fix frame limiter to pinned GameFrame grBufferSwap")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_fps_limit.py <game-frame checkout>")
    install(pathlib.Path(sys.argv[1]))
