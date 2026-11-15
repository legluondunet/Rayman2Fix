#!/usr/bin/env python3
"""Opt-in, bounded capture of gold-hued source textures for the Rayman 2 intro.

Installed AFTER the already-pinned render-probe and loading-art patches.
The standard render path remains byte-identical: no texture filtering,
texture cache changes, palette replacements, or draw calls are modified.
"""
from __future__ import annotations

import pathlib
import sys

MODULE = r'''//! Rayman2Fix: optional capture of likely intro-title texture tiles.
//! Only enabled with RAYMAN2FIX_LOGO_CAPTURE=1, never by default.
//! Writes user-owned decoded pixels to local BMPs; does not edit game files.
use std::collections::HashSet;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::sync::{Mutex, OnceLock};

const MAX_DIMENSION: usize = 512;
const MAX_CANDIDATES: usize = 96;
const MAX_CAPTURE_BYTES: usize = 48 * 1024 * 1024;

#[derive(Default)]
struct Capture {
    hashes: HashSet<u64>,
    bytes: usize,
}

fn enabled() -> bool {
    static ACTIVE: OnceLock<bool> = OnceLock::new();
    *ACTIVE.get_or_init(|| {
        std::env::var("RAYMAN2FIX_LOGO_CAPTURE").ok().as_deref() == Some("1")
    })
}

fn parent() -> Option<std::path::PathBuf> {
    let cwd = std::env::current_dir().ok()?;
    Some(cwd.parent().unwrap_or(&cwd).to_path_buf())
}

fn log(message: &str) {
    let Some(parent) = parent() else { return };
    let file = parent.join("Rayman2Fix.log");
    if let Ok(mut writer) = OpenOptions::new().append(true).open(file) {
        let _ = writeln!(writer, "RUNTIME-GLIDE: LOGO-CAPTURE: {message}");
    }
}

fn gold_pixels(w: usize, h: usize, rgba: &[u8]) -> usize {
    if w < 16 || h < 16 || w > MAX_DIMENSION || h > MAX_DIMENSION
        || rgba.len() != w * h * 4 {
        return 0;
    }
    rgba.chunks_exact(4).filter(|p| {
        let (r,g,b,a)=(p[0],p[1],p[2],p[3]);
        a >= 32 && r >= 130 && g >= 74
            && (r as u32) * 100 >= (g as u32) * 90
            && (g as u16) >= (b as u16) + 24
            && (r as u16) >= (b as u16) + 56
    }).count()
}

fn fnv64(rgba: &[u8]) -> u64 {
    let mut result: u64 = 0xcbf29ce484222325;
    for &b in rgba {
        result ^= u64::from(b);
        result = result.wrapping_mul(0x100000001b3);
    }
    result
}

fn write_bmp(path: &std::path::Path, w: usize, h: usize, rgba: &[u8]) -> std::io::Result<()> {
    // 32-bit BI_RGB top-down bitmap, no dependencies, one RGBA pixel per
    // source texel. No interpolation or material/alpha transformation.
    let payload = w * h * 4;
    let mut bytes = vec![0u8; 54 + payload];
    let total = u32::try_from(bytes.len()).unwrap_or(0);
    bytes[0..2].copy_from_slice(b"BM");
    bytes[2..6].copy_from_slice(&total.to_le_bytes());
    bytes[10..14].copy_from_slice(&54u32.to_le_bytes());
    bytes[14..18].copy_from_slice(&40u32.to_le_bytes());
    bytes[18..22].copy_from_slice(&(w as i32).to_le_bytes());
    bytes[22..26].copy_from_slice(&(-(h as i32)).to_le_bytes());
    bytes[26..28].copy_from_slice(&1u16.to_le_bytes());
    bytes[28..30].copy_from_slice(&32u16.to_le_bytes());
    bytes[34..38].copy_from_slice(&(payload as u32).to_le_bytes());
    // Pixel channels in BMP files are BGRA, but stored top-down.
    for (src,dst) in rgba.chunks_exact(4).zip(bytes[54..].chunks_exact_mut(4)) {
        dst.copy_from_slice(&[src[2],src[1],src[0],src[3]]);
    }
    fs::write(path, bytes)
}

/// Called only after GameFrame's original texel decoder succeeded on
/// the largest mip level. No pixel is modified, read across threads,
/// or ever replaced at the OpenGL API boundary.
pub fn inspect(w: usize, h: usize, format: u32, rgba: &[u8]) {
    if !enabled() { return; }
    let gold = gold_pixels(w,h,rgba);
    if gold < 32 || gold * 100 < w * h { return; }
    let hash = fnv64(rgba);
    static STATE: OnceLock<Mutex<Capture>> = OnceLock::new();
    let state = STATE.get_or_init(|| Mutex::new(Capture::default()));
    let Ok(mut state) = state.lock() else { return };
    if state.hashes.contains(&hash) || state.hashes.len() >= MAX_CANDIDATES
       || state.bytes.saturating_add(rgba.len()) > MAX_CAPTURE_BYTES {
        return;
    }
    state.hashes.insert(hash);
    state.bytes+=rgba.len();
    let n = state.hashes.len();
    drop(state);
    let Some(base) = parent() else { return };
    let folder = base.join("Rayman2Fix_logo_capture");
    if fs::create_dir_all(&folder).is_err() { return; }
    let file = format!("tile_{n:03}_{w}x{h}_fmt{format}_{hash:016x}.bmp");
    if write_bmp(&folder.join(&file),w,h,rgba).is_ok() {
        if n == 1 {
            log("opt-in texture sampling active; candidate BMPs are ORIGINAL copyrighted game pixels, not replacements");
        }
        log(&format!("candidate={n} size={w}x{h} format={format} gold_pixels={gold} fnv64={hash:016x} file={file} (read-only; tile identity unknown)"));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_gold_opaque_source_pixels_qualify() {
        let gold = vec![255u8,190u8,20u8,255u8].repeat(32*32);
        assert_eq!(gold_pixels(32,32,&gold),1024);
        let blue = vec![0u8,50u8,200u8,255u8].repeat(32*32);
        assert_eq!(gold_pixels(32,32,&blue),0);
        let alpha_zero = vec![255u8,190u8,20u8,0u8].repeat(32*32);
        assert_eq!(gold_pixels(32,32,&alpha_zero),0);
        assert_eq!(gold_pixels(1,1024,&gold),0);
    }
    #[test]
    fn bmp_header_and_rgb_to_bgr_are_stable() {
        let dir=std::env::temp_dir();
        let path=dir.join(format!("r2fix-capture-test-{}.bmp",std::process::id()));
        let sample=vec![250,150,4,255].repeat(16*16);
        write_bmp(&path,16,16,&sample).unwrap();
        let raw=fs::read(&path).unwrap();
        assert_eq!(&raw[0..2],b"BM");
        assert_eq!(&raw[54..58], &[4u8,150u8,250u8,255u8]);
        let _=fs::remove_file(path);
    }
}
'''

def replace_once(src: str, old: str, new: str) -> str:
    if src.count(old)!=1:
        raise SystemExit(f"Pinned GameFrame changed (expected one): {old[:95]!r}")
    return src.replace(old,new)

def install(root: pathlib.Path) -> None:
    base = root / "wrappers" / "glide" / "src"
    lib = base / "lib.rs"
    texture = base / "render" / "textures.rs"
    module = base / "intro_logo_capture.rs"
    if not (lib.is_file() and texture.is_file()) or module.exists():
        raise SystemExit("Pinned GameFrame files missing or intro capture installed twice")
    a,b = lib.read_text(encoding="utf-8"),texture.read_text(encoding="utf-8")
    a = replace_once(a, "#[cfg(windows)]\nmod loading_image_fix;",
        "#[cfg(windows)]\nmod loading_image_fix;\n#[cfg(windows)]\nmod intro_logo_capture;")
    b = replace_once(b,
        '                    Ok(img) => unsafe {\n                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n                    },',
        '                    Ok(img) => unsafe {\n                        if k == 0 { crate::intro_logo_capture::inspect(lw,lh,i.format,&img.pixels); }\n                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n                    },'
    )
    # All fingerprints and source anchors match; commit changes atomically.
    lib.write_text(a,encoding="utf-8")
    texture.write_text(b,encoding="utf-8")
    module.write_text(MODULE,encoding="utf-8")
    print("Installed disabled-by-default intro-logo source texture capture; no pixel modifications")

if __name__ == "__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_intro_logo_capture.py <pinned GameFrame checkout>")
    install(pathlib.Path(sys.argv[1]))
