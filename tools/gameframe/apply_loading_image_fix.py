#!/usr/bin/env python3
"""Rayman2Fix loading art correction in pinned GameFrame, without altering 3D.

The game's 1024x768 loading image is an RGB565 CPU framebuffer upload
while Glide reports an ABGR color format. GameFrame currently swaps R/B
for ALL ABGR LFB pixels, making this specific loading artwork blue.
The full-screen 2D image is also expanded from 4:3 to the native wide
window. Fix ONLY this exact 1024x768 RGB565+ABGR full-screen transfer.
"""
from __future__ import annotations

import pathlib
import sys

MODULE = r'''//! Rayman2Fix-specific, fully qualified CPU loading-art correction.
//! Does not touch 3D vertices, GPU textures, video playback or HUD sprites.

use crate::consts::{COLORFORMAT_ABGR, LFB_565};

pub fn is_loading_art(w: u32, h: u32, x0: u32, y0: u32,
                      x1: u32, y1: u32, mode: u32, format: u32) -> bool {
    w == 1024 && h == 768 &&
    x0 == 0 && y0 == 0 && x1 == w && y1 == h &&
    mode == LFB_565 && format == COLORFORMAT_ABGR
}

/// The 2D rectangle is expressed in game-native pixels, and still drawn in
/// the full-size physical FBO. Preserve height; contract X about the center
/// to the 4:3 viewport only for recognized full-screen loading art.
pub fn rect(loading_art: bool, x: u32, y: u32, w: u32, h: u32,
            sw: u32, sh: u32, output_w: u32, output_h: u32) -> [f32; 4] {
    let (mut x0, mut x1) = (x as f32, (x + w) as f32);
    if loading_art && sw > 0 && sh > 0 && output_w > 0 && output_h > 0 {
        let screen_aspect = sw as f32 / sh as f32;
        let output_aspect = output_w as f32 / output_h as f32;
        if output_aspect > screen_aspect {
            let scale = screen_aspect / output_aspect;
            let middle = sw as f32 * 0.5;
            x0 = middle + (x0 - middle) * scale;
            x1 = middle + (x1 - middle) * scale;
        }
    }
    [x0, y as f32, x1, (y + h) as f32]
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn only_cpu_loading_picture_is_selected() {
        assert!(is_loading_art(1024,768,0,0,1024,768,LFB_565,COLORFORMAT_ABGR));
        assert!(!is_loading_art(1024,768,0,0,1024,768,LFB_565,0));
        assert!(!is_loading_art(1024,768,0,0,1023,768,LFB_565,COLORFORMAT_ABGR));
        assert!(!is_loading_art(640,480,0,0,640,480,LFB_565,COLORFORMAT_ABGR));
    }
    #[test]
    fn letterbox_preserves_vertical_size_and_centers() {
        let r = rect(true,0,0,1024,768,1024,768,1920,1080);
        assert!((r[0] - 128.0).abs() < 0.01);
        assert!((r[2] - 896.0).abs() < 0.01);
        assert_eq!(r[1],0.0);
        assert_eq!(r[3],768.0);
        assert_eq!(rect(true,0,0,1024,768,1024,768,1280,960),
                   [0.0,0.0,1024.0,768.0]);
        assert_eq!(rect(false,0,0,1024,768,1024,768,1920,1080),
                   [0.0,0.0,1024.0,768.0]);
    }
}
'''

def replace_exact(text: str, old: str, new: str, expected: int = 1) -> str:
    if text.count(old) != expected:
        raise SystemExit(
            f"Pinned GameFrame changed: expected {expected} occurrences of {old[:85]!r}")
    return text.replace(old, new)

def install(root: pathlib.Path) -> None:
    base = root / "wrappers" / "glide" / "src"
    lib = base / "lib.rs"
    cmd = base / "cmd.rs"
    lfb = base / "api" / "lfb.rs"
    render = base / "render" / "mod.rs"
    module = base / "loading_image_fix.rs"
    paths = (lib, cmd, lfb, render)
    if not all(p.is_file() for p in paths) or module.exists():
        raise SystemExit("Pinned GameFrame files missing or patch already installed")
    a,b,c,d = (p.read_text(encoding="utf-8") for p in paths)
    a = replace_exact(a, "#[cfg(windows)]\nmod render_probe;",
                      "#[cfg(windows)]\nmod render_probe;\n#[cfg(windows)]\nmod loading_image_fix;")
    b = replace_exact(b,
                      "    pub rgba: Vec<u8>,\n    /// Through the pixel pipeline",
                      "    pub rgba: Vec<u8>,\n"
                      "    /// True only for the original RGB565/ABGR full-size 1024x768 loading image.\n"
                      "    pub four_three_loading: bool,\n"
                      "    /// Through the pixel pipeline")
    c = replace_exact(c,
                      "    let alpha = if l.bypass { 0xff } else { l.constant_alpha };\n"
                      "    let mut rgba_out = Vec::with_capacity((x1 - x0) * (y1 - y0) * 4);",
                      "    let alpha = if l.bypass { 0xff } else { l.constant_alpha };\n"
                      "    let loading_art = crate::loading_image_fix::is_loading_art(\n"
                      "        w, h, x0 as u32, y0 as u32, x1 as u32, y1 as u32,\n"
                      "        l.mode, l.format);\n"
                      "    if loading_art { crate::render_probe::loading_corrected(); }\n"
                      "    let mut rgba_out = Vec::with_capacity((x1 - x0) * (y1 - y0) * 4);")
    c = replace_exact(c,
                      "                    let mut c = rgba(l.mode, l.format, v, alpha);",
                      "                    // The game's loading DIB supplies RGB565 despite the\n"
                      "                    // ABGR Glide mode: avoid the LFB-only R/B swap.\n"
                      "                    let mut c = if loading_art {\n"
                      "                        rgba_argb(l.mode, v, alpha)\n"
                      "                    } else { rgba(l.mode, l.format, v, alpha) };")
    c = replace_exact(c,
                      "    Some(LfbImage { x: x0 as u32, y: y0 as u32, w: (x1 - x0) as u32, h: (y1 - y0) as u32, rgba: rgba_out, pipeline: !l.bypass })",
                      "    Some(LfbImage { x: x0 as u32, y: y0 as u32, w: (x1 - x0) as u32,\n"
                      "        h: (y1 - y0) as u32, rgba: rgba_out,\n"
                      "        four_three_loading: loading_art, pipeline: !l.bypass })")
    d = replace_exact(d,
                      "            let (x, y) = (img.x as f32, img.y as f32);\n"
                      '            (gl.Uniform4f)(p.loc("uRect"), x, y, x + img.w as f32, y + img.h as f32);',
                      "            let rect = crate::loading_image_fix::rect(\n"
                      "                img.four_three_loading, img.x, img.y, img.w, img.h,\n"
                      "                g.frame.w, g.frame.h, g.frame.ow, g.frame.oh);\n"
                      '            (gl.Uniform4f)(p.loc("uRect"), rect[0], rect[1], rect[2], rect[3]);')
    d = replace_exact(d,
                      "            (gl.ColorMask)(1, 1, 1, 1);\n"
                      "            (gl.DrawArrays)(0x0005, 0, 4);",
                      "            (gl.ColorMask)(1, 1, 1, 1);\n"
                      "            // Clear the full buffer once per loading picture so that\n"
                      "            // pillarbox margins remain black, never stale 3D pixels.\n"
                      "            // No artificial side quads and no impact on gameplay frames.\n"
                      "            if img.four_three_loading {\n"
                      "                (gl.ClearColor)(0.0, 0.0, 0.0, 1.0);\n"
                      "                (gl.Clear)(COLOR_BUFFER_BIT);\n"
                      "            }\n"
                      "            (gl.DrawArrays)(0x0005, 0, 4);")
    # Commit changes only after all source anchors have been checked.
    for path,text in zip(paths,(a,b,c,d)):
        path.write_text(text,encoding="utf-8")
    module.write_text(MODULE,encoding="utf-8")
    print("Applied Rayman2Fix RGB565 loading image colors and centered 4:3 canvas")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_loading_image_fix.py <game-frame checkout>")
    install(pathlib.Path(sys.argv[1]))
