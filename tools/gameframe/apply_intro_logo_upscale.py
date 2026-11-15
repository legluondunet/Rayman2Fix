#!/usr/bin/env python3
"""4x RGBA interpolation for 18 exact retail-PC Rayman 2 intro logo tiles.

Applied after the pinned GameFrame render probe and loading-image patch.
Only captured 32x32 format-12 fingerprints match. No original asset data
is redistributed. The remaining textures follow the exact old upload.
"""
from __future__ import annotations
import pathlib
import sys

MODULE = r'''//! Reconstruction of the approved original Rayman 2 CD's logo
//! texture data at upload time, without embedding Ubisoft art.
//! Exact hash guard prevents all other objects from being modified.
use std::collections::HashSet;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::{Mutex,OnceLock};

const TITLE_HASHES: [u64;18] = [
    0xc779121fead9b616,0xd7e0350fa4cb9c4a,0xdfd19ea276cfbac8,
    0xb52fbe2bd8536fb9,0x72873cc31df18e61,0xa5044235110a6276,
    0x98dd6d5017f6b7cb,0x6e3506d183af3814,0x092b6e838d3494ae,
    0x8475d646160c4de3,0x0cd885a2998ac830,0x1a8beb91e5c36fc1,
    0x192d2c32c1ae1387,0x67322b296eca0106,0xfa4513902d9db372,
    0x9f91633407171360,0x4ff3c2621974f6be,0xa8d3d5801e9e09b5,
];

fn fnv64(bytes:&[u8])->u64 {
    let mut h=0xcbf29ce484222325u64;
    for &b in bytes {h^=b as u64;h=h.wrapping_mul(0x100000001b3);}
    h
}

fn log_once(hash:u64) {
    static SEEN:OnceLock<Mutex<HashSet<u64>>>=OnceLock::new();
    let Ok(mut seen)=SEEN.get_or_init(||Mutex::new(HashSet::new())).lock() else {return;};
    if !seen.insert(hash) {return;}
    let Ok(cwd)=std::env::current_dir() else {return;};
    let Some(base)=cwd.parent() else {return;};
    let Ok(mut file)=OpenOptions::new().append(true).open(base.join("Rayman2Fix.log")) else {return;};
    let _=writeln!(file,"RUNTIME-GLIDE: INTRO-LOGO-UPSCALER: match=0x{hash:016x} source=32x32 format=12 upload=128x128 alpha=premultiplied (original source retained)");
}

pub fn is_title_tile(w:usize,h:usize,format:u32,rgba:&[u8])->bool {
    if w!=32 || h!=32 || format!=12 || rgba.len()!=4096 {return false;}
    let hash=fnv64(rgba);
    if !TITLE_HASHES.contains(&hash) {return false;}
    log_once(hash);
    true
}

fn cubic(delta:f32)->f32 {
    let t=delta.abs();
    if t<1.0 {1.5*t*t*t-2.5*t*t+1.0}
    else if t<2.0 {-0.5*t*t*t+2.5*t*t-4.0*t+2.0}
    else {0.0}
}

/// Upscale each mip level by the SAME fixed factor; the original
/// 32x32 Glide *logical* size and UV coordinates remain untouched.
/// Premultiplied RGB avoids dark halos at alpha-transparent edges.
pub fn resample_rgba(w:usize,h:usize,src:&[u8])->Option<Vec<u8>> {
    if w==0 || h==0 || w>32 || h>32 || src.len()!=w*h*4 {return None;}
    let (ow,oh)=(w*4,h*4);
    let mut result=vec![0u8;ow*oh*4];
    for y in 0..oh {
        let fy=(y as f32+0.5)/4.0-0.5;
        let iy=fy.floor() as i32;
        for x in 0..ow {
            let fx=(x as f32+0.5)/4.0-0.5;
            let ix=fx.floor() as i32;
            let (mut alpha,mut rgb)=(0.0f32,[0.0f32;3]);
            for dy in -1..=2 {
                let sy=(iy+dy).clamp(0,h as i32-1) as usize;
                let wy=cubic(fy-(iy+dy) as f32);
                for dx in -1..=2 {
                    let sx=(ix+dx).clamp(0,w as i32-1) as usize;
                    let weight=wy*cubic(fx-(ix+dx) as f32);
                    let i=(sy*w+sx)*4;
                    let a=src[i+3] as f32/255.0;
                    alpha+=weight*a;
                    for c in 0..3 {rgb[c]+=weight*a*(src[i+c] as f32/255.0);}
                }
            }
            let i=(y*ow+x)*4;
            result[i+3]=(alpha.clamp(0.0,1.0)*255.0).round() as u8;
            if alpha>0.0001 {
                for c in 0..3 {
                    result[i+c]=((rgb[c]/alpha).clamp(0.0,1.0)*255.0).round() as u8;
                }
            }
        }
    }
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_18_fingerprints_unique() {
        let mut v=TITLE_HASHES.to_vec();v.sort_unstable();v.dedup();
        assert_eq!(v.len(),18);
    }
    #[test]
    fn wrong_size_format_or_pixels_never_match() {
        let pix=vec![128u8;4096];
        assert!(!is_title_tile(32,32,12,&pix));
        assert!(!is_title_tile(16,32,12,&pix));
        assert!(!is_title_tile(32,32,11,&pix));
        assert!(!is_title_tile(32,32,12,&pix[..1024]));
    }
    #[test]
    fn resampling_preserves_constant_colors_and_opacity() {
        let gold=vec![255u8,175u8,15u8,255u8].repeat(32*32);
        let out=resample_rgba(32,32,&gold).unwrap();
        assert_eq!(out.len(),128*128*4);
        for pix in out.chunks_exact(4) {
            assert_eq!(pix,&[255,175,15,255]);
        }
        let clear=vec![0u8;32*32*4];
        let out=resample_rgba(32,32,&clear).unwrap();
        assert!(out.iter().all(|&x|x==0));
        assert!(resample_rgba(0,32,&[]).is_none());
        assert!(resample_rgba(33,32,&gold).is_none());
    }
    #[test]
    fn transparent_edges_do_not_introduce_black_rgb_fringe() {
        let mut p=vec![0u8;16*4];
        p[20..24].copy_from_slice(&[255,170,30,255]);
        let o=resample_rgba(4,4,&p).unwrap();
        for c in o.chunks_exact(4) {
            if c[3]>0 {
                assert!(c[0]>=248 && c[1]>=163 && c[2]>=27);
            }
        }
    }
}
'''

def substitute(s:str,old:str,new:str)->str:
    if s.count(old)!=1:raise SystemExit("Pinned GameFrame changed: "+repr(old[:100]))
    return s.replace(old,new)

def install(root:pathlib.Path)->None:
    base=root/"wrappers"/"glide"/"src"
    lib=base/"lib.rs"
    tex=base/"render"/"textures.rs"
    module=base/"intro_logo_upscale.rs"
    if not lib.is_file() or not tex.is_file() or module.exists():
        raise SystemExit("Required GameFrame files missing or already patched")
    a,b=lib.read_text(encoding="utf-8"),tex.read_text(encoding="utf-8")
    a=substitute(a,"#[cfg(windows)]\nmod loading_image_fix;",
        "#[cfg(windows)]\nmod loading_image_fix;\n#[cfg(windows)]\nmod intro_logo_upscale;")
    b=substitute(b,
        "            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {",
        "            // Keep original logical UV scale. If base level matches,\n"
        "            // enlarge all mip levels together for consistent GPU sampling.\n"
        "            let mut title_tile=false;\n"
        "            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {")
    b=substitute(b,
        "                    Ok(img) => unsafe {\n"
        "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
        "                    },",
        "                    Ok(img) => unsafe {\n"
        "                        if k==0 {title_tile=crate::intro_logo_upscale::is_title_tile(lw,lh,i.format,&img.pixels);}\n"
        "                        if title_tile {\n"
        "                            if let Some(up)=crate::intro_logo_upscale::resample_rgba(lw,lh,&img.pixels) {\n"
        "                                (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,(lw*4) as i32,(lh*4) as i32,0,RGBA,UNSIGNED_BYTE,up.as_ptr().cast());\n"
        "                            } else {\n"
        "                                (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());\n"
        "                            }\n"
        "                        } else {\n"
        "                            (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());\n"
        "                        }\n"
        "                    },")
    lib.write_text(a,encoding="utf-8")
    tex.write_text(b,encoding="utf-8")
    module.write_text(MODULE,encoding="utf-8")
    print("Installed fingerprint-guarded 4x title interpolation; unknown textures unchanged")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: apply_intro_logo_upscale.py <GameFrame-root>")
    install(pathlib.Path(sys.argv[1]))
