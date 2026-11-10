#!/usr/bin/env python3
"""Small read-only Glide diagnostic for Rayman2Fix's pinned GameFrame checkout.

Appends a capped number of display-format and framebuffer-transfer summaries
to the launcher log (only when Debug.Log=1). No pixel data, code addresses,
personal details or separate log files. Does not modify any rendering output.
"""
from __future__ import annotations

import pathlib
import sys

MODULE = r'''//! Rayman2Fix: limited Glide 2D colour-path diagnostics only.
//! The renderer's pixels and state are never modified here.
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::OnceLock;
use std::sync::atomic::{AtomicU32, Ordering};

static OPENS: AtomicU32 = AtomicU32::new(0);
static MODES: AtomicU32 = AtomicU32::new(0);
static FORMATS: AtomicU32 = AtomicU32::new(0);
static WRITES: AtomicU32 = AtomicU32::new(0);
static POINTERS: AtomicU32 = AtomicU32::new(0);
static PICTURES: AtomicU32 = AtomicU32::new(0);
static TEXTURES: AtomicU32 = AtomicU32::new(0);
static PALETTES: AtomicU32 = AtomicU32::new(0);

fn active() -> bool {
    static ENABLED: OnceLock<bool> = OnceLock::new();
    *ENABLED.get_or_init(|| std::env::var("RAYMAN2FIX_GLIDE_PROBE").ok().as_deref() == Some("1"))
}
fn line(message: &str) {
    if !active() { return; }
    let Ok(cwd) = std::env::current_dir() else { return };
    let Some(parent) = cwd.parent() else { return };
    let log = parent.join("Rayman2Fix.log");
    if let Ok(mut file) = OpenOptions::new().append(true).open(log) {
        let _ = writeln!(file, "RUNTIME-GLIDE: {message}");
    }
}
pub fn loading_corrected() {
    static ONCE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    if !ONCE.swap(true, Ordering::Relaxed) {
        line("1024x768 loading art RGB565/ABGR: colors restored, 4:3 canvas centered");
    }
}
pub fn opened(w: u32, h: u32, fmt: u32, origin: u32) {
    if OPENS.fetch_add(1, Ordering::Relaxed) < 5 {
        line(&format!("screen {w}x{h} format={fmt} origin={origin}"));
    }
}
pub fn write_mode(mode: u32) {
    if MODES.fetch_add(1, Ordering::Relaxed) < 7 {
        line(&format!("LFB write mode={mode}"));
    }
}
pub fn write_format(format: u32) {
    if FORMATS.fetch_add(1, Ordering::Relaxed) < 7 {
        line(&format!("LFB write color format={format}"));
    }
}
pub fn write_ptr(buffer: u32) {
    if POINTERS.fetch_add(1, Ordering::Relaxed) < 3 {
        line(&format!("LFB write pointer requested buffer={buffer}"));
    }
}
pub fn region(api: &str, w: u32, h: u32, mode: u32, format: u32) {
    if w < 256 || h < 8 { return; }
    if WRITES.fetch_add(1, Ordering::Relaxed) < 16 {
        line(&format!("{api} {w}x{h} mode={mode} color_format={format}"));
    }
}
pub fn picture(w: u32, h: u32, mode: u32, format: u32) {
    if w < 256 || h < 8 { return; }
    if PICTURES.fetch_add(1, Ordering::Relaxed) < 12 {
        line(&format!("LFB pixels {w}x{h} mode={mode} color_format={format}"));
    }
}
pub fn texture(format: u32, large: u32, small: u32) {
    if TEXTURES.fetch_add(1, Ordering::Relaxed) < 6 {
        line(&format!("texture upload format={format} lod={large}..{small}"));
    }
}
pub fn palette(kind: u32, first: i32, last: i32) {
    if PALETTES.fetch_add(1, Ordering::Relaxed) < 4 {
        line(&format!("palette upload kind={kind} entries={first}..{last}"));
    }
}
'''

def substitute(source: str, old: str, new: str, count: int = 1) -> str:
    if source.count(old) != count:
        raise SystemExit(f"Pinned GameFrame layout changed (need {count} copies): {old!r}")
    return source.replace(old, new, count)

def install(root: pathlib.Path) -> None:
    source = root / "wrappers" / "glide" / "src"
    lib = source / "lib.rs"
    sst = source / "api" / "sst.rs"
    lfb = source / "api" / "lfb.rs"
    tex = source / "api" / "tex.rs"
    probe = source / "render_probe.rs"
    if not all(p.is_file() for p in (lib, sst, lfb, tex)) or probe.exists():
        raise SystemExit("Pinned GameFrame sources missing or probe already installed")
    a, b, c, d = (p.read_text(encoding="utf-8") for p in (lib, sst, lfb, tex))
    a = substitute(a, "#[cfg(windows)]\nmod fps_limit;",
                   "#[cfg(windows)]\nmod fps_limit;\n#[cfg(windows)]\nmod render_probe;")
    b = substitute(b, "    let (ow, oh) = crate::config::get().output(w, h);",
                   "    crate::render_probe::opened(w, h, format, origin);\n"
                   "    let (ow, oh) = crate::config::get().output(w, h);", 2)
    c = substitute(c, "    shim::call!(\"grLfbWriteMode\", mode);",
                   "    shim::call!(\"grLfbWriteMode\", mode);\n"
                   "    crate::render_probe::write_mode(mode);")
    c = substitute(c, "    shim::call!(\"grLfbWriteColorFormat\", format);",
                   "    shim::call!(\"grLfbWriteColorFormat\", format);\n"
                   "    crate::render_probe::write_format(format);")
    c = substitute(c, "    shim::call!(\"grLfbGetWritePtr\", buffer);",
                   "    shim::call!(\"grLfbGetWritePtr\", buffer);\n"
                   "    crate::render_probe::write_ptr(buffer);")
    c = substitute(c, "    if let Some(img) = img {\n        // The pixels",
                   "    if let Some(img) = img {\n"
                   "        let (mode, fmt) = with(|l| (l.mode, l.format));\n"
                   "        crate::render_probe::picture(img.w, img.h, mode, fmt);\n"
                   "        // The pixels")
    c = substitute(c, "    shim::call!(\"guFbWriteRegion\", x, y, w, h, crate::callargs::Region(src, w, h, stride, with(|l| if wide(l.mode) { 4 } else { 2 })), stride);\n    finish_write();",
                   "    shim::call!(\"guFbWriteRegion\", x, y, w, h, crate::callargs::Region(src, w, h, stride, with(|l| if wide(l.mode) { 4 } else { 2 })), stride);\n"
                   "    let (mode, fmt) = with(|l| (l.mode, l.format));\n"
                   "    crate::render_probe::region(\"guFbWriteRegion\", w, h, mode, fmt);\n"
                   "    finish_write();")
    d = substitute(d, "    let Some(i) = (unsafe { info.as_ref() }) else { return };",
                   "    let Some(i) = (unsafe { info.as_ref() }) else { return };\n"
                   "    crate::render_probe::texture(i.format, i.large_lod, i.small_lod);")
    d = substitute(d,
                   '    shim::call!("grTexDownloadTablePartial", tmu, kind, crate::callargs::table(kind, data, first, last), first, last);',
                   '    shim::call!("grTexDownloadTablePartial", tmu, kind, crate::callargs::table(kind, data, first, last), first, last);\n'
                   "    crate::render_probe::palette(kind, first, last);")
    # All replacements have been verified before mutating any upstream file.
    for path, contents in ((lib,a),(sst,b),(lfb,c),(tex,d)):
        path.write_text(contents,encoding="utf-8")
    probe.write_text(MODULE,encoding="utf-8")
    print("Applied read-only Rayman2Fix Glide color-path diagnostics (single log)")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_render_probe.py <game-frame checkout>")
    install(pathlib.Path(sys.argv[1]))
