#!/usr/bin/env python3
"""Strictly fingerprint-locked, optional hi-res Rayman 2 logo texture override.

Applied to the pinned GameFrame checkout, after the already tested
loading-image correction. No copyrighted bitmap is bundled.
"""
from __future__ import annotations
import pathlib
import sys

MODULE = r'''//! Optional native hi-res Rayman 2 intro logo assembled by the user.
//! Exact fingerprints and a fixed 6x3 mapping to the *original* texture
//! tiles; unknown sources and missing/invalid assets stay unchanged.
use std::collections::HashSet;
use std::fs::{self,OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex,OnceLock};

const WIDTH:usize=768;
const HEIGHT:usize=384;
const TILE:usize=128;
const MAGIC:&[u8;8]=b"R2LOGOHD";
const BYTES:usize=16+WIDTH*HEIGHT*4;

/// FNV-1a64 of the 18 decoded 32x32, Glide format-12 tiles
/// captured directly from the user's approved retail CD.
const TITLE_HASHES:[u64;18]=[
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

fn log_once(key:u64, msg:&str) {
    static SEEN:OnceLock<Mutex<HashSet<u64>>>=OnceLock::new();
    let Ok(mut seen)=SEEN.get_or_init(||Mutex::new(HashSet::new())).lock() else {return;};
    if !seen.insert(key) {return;}
    let Ok(cwd)=std::env::current_dir() else {return;};
    let parent=cwd.parent().unwrap_or(&cwd);
    if let Ok(mut file)=OpenOptions::new().append(true).open(parent.join("Rayman2Fix.log")) {
        let _=writeln!(file,"RUNTIME-GLIDE: INTRO-LOGO-HD: {msg}");
    }
}

/// Bounded read-only diagnostics on the first GameFrame texture upload
/// and the first 32x32 / format-12 tile. Distinguishes a wrong bundle,
/// unrecognized source, mismatched mip count and missing local atlas.
pub fn observe_source(w:usize,h:usize,format:u32,mips:usize) {
    log_once(0x1000,&format!("renderer hook ACTIVE; first decoded texture={}x{} format={} mip_count={}; atlas path checked only on source match",w,h,format,mips));
    if w==32 && h==32 && format==12 {
        log_once(0x1001,&format!("observed source texture 32x32 format=12 mip_count={}",mips));
    }
}

fn load_atlas() -> Option<Vec<u8>> {
    let cwd=std::env::current_dir().ok()?;
    let mut paths=vec![cwd.join("Rayman2Fix_logo_hd.rgba")];
    if let Some(p)=cwd.parent(){ paths.push(p.join("Rayman2Fix_logo_hd.rgba")); }
    if let Ok(exe)=std::env::current_exe() {
        if let Some(p)=exe.parent(){ paths.push(p.join("Rayman2Fix_logo_hd.rgba")); }
    }
    for path in paths {
        let Ok(meta)=fs::metadata(&path) else {continue};
        if meta.len()!=BYTES as u64 {
            log_once(1,&format!("ignored invalid asset size={} expected={} at {}",meta.len(),BYTES,path.display()));
            continue;
        }
        let Ok(data)=fs::read(&path) else {continue};
        if data.len()!=BYTES || &data[..8]!=MAGIC
            || u32::from_le_bytes(data[8..12].try_into().unwrap_or([0;4]))!=WIDTH as u32
            || u32::from_le_bytes(data[12..16].try_into().unwrap_or([0;4]))!=HEIGHT as u32 {
            log_once(2,"ignored invalid asset header or dimensions");
            continue;
        }
        log_once(3,&format!("external user-supplied atlas loaded {}x{} from {}",WIDTH,HEIGHT,path.display()));
        return Some(data);
    }
    log_once(4,"optional user-supplied atlas not found; original title texture uploads preserved");
    None
}

fn atlas()->Option<&'static [u8]> {
    static ATLAS:OnceLock<Option<Vec<u8>>>=OnceLock::new();
    ATLAS.get_or_init(load_atlas).as_deref()
}

/// Source tiles were recorded bottom-up. The 6x3 upright full logo
/// was reconstructed by exact matching of all horizontal/vertical
/// tile edges and the readable "THE GREAT ESCAPE" row:
///
///   row 0: 34 31 28 25 22 19
///   row 1: 33 30 27 24 21 18
///   row 2: 32 29 26 23 20 17
///
/// The rendering engine owns all positioning, alpha blending and fade
/// transitions; we ONLY replace source texels at original draw calls.
pub fn atlas_cell(index:usize)->Option<(usize,usize)> {
    if index>=TITLE_HASHES.len(){return None;}
    let n=17-index;
    Some((n/3,n%3))
}

fn tile_pixels(master:&[u8],col:usize,row:usize)->Option<Vec<u8>> {
    if master.len()!=BYTES || &master[..8]!=MAGIC || col>=6 || row>=3 {return None;}
    let mut out=vec![0u8;TILE*TILE*4];
    for y in 0..TILE {
        // Original source tiles are vertically flipped relative to
        // the upright logo; do not change game UVs.
        let from=16+((row*TILE+(TILE-1-y))*WIDTH+col*TILE)*4;
        let to=y*TILE*4;
        out[to..to+TILE*4].copy_from_slice(&master[from..from+TILE*4]);
    }
    Some(out)
}

/// On a match, the returned RGBA pixels are 128x128 but the caller
/// continues to report logical 32x32 to the shader for unchanged UVs.
/// If any precondition fails, return None for byte-identical behavior.
pub fn replacement(w:usize,h:usize,format:u32,rgba:&[u8],single_mip:bool)->Option<Vec<u8>> {
    if w!=32 || h!=32 || format!=12 || rgba.len()!=4096 {return None;}
    if !single_mip {
        log_once(0x1002,"32x32 format-12 original rejected: texture has more than one mip level");
        return None;
    }
    let hash=fnv64(rgba);
    let Some(idx)=TITLE_HASHES.iter().position(|&v|v==hash) else {
        log_once(0x1003,&format!("first 32x32 format-12 source is not in the original 18: hash={hash:016x}; continuing to search"));
        return None;
    };
    log_once(0x1004,&format!("recognized original logo tile={} hash={hash:016x}; checking local HD atlas",idx+17));
    let Some(master)=atlas() else {
        log_once(0x1005,"recognized source logo tile but HD atlas is not readable/valid; preserving original pixels");
        return None;
    };
    let Some((col,row))=atlas_cell(idx) else {return None};
    let Some(result)=tile_pixels(master,col,row) else {
        log_once(0x1006,"HD atlas tile extraction failed; preserving original pixels");
        return None;
    };
    log_once(hash,&format!("tile={} hash={hash:016x} slot=({}, {}) original=32x32 gpu=128x128 alpha=original draw blending",idx+17,col,row));
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fingerprint_set_and_full_tile_topology() {
        let mut a=TITLE_HASHES.to_vec();a.sort_unstable();a.dedup();
        assert_eq!(a.len(),18);
        assert_eq!(atlas_cell(0),Some((5,2)));
        assert_eq!(atlas_cell(1),Some((5,1)));
        assert_eq!(atlas_cell(2),Some((5,0)));
        assert_eq!(atlas_cell(17),Some((0,0)));
        assert_eq!(atlas_cell(18),None);
    }
    #[test]
    fn tile_slicing_flips_source_vertically_and_preserves_alpha() {
        let mut master=vec![0u8;BYTES];
        master[..8].copy_from_slice(MAGIC);
        master[8..12].copy_from_slice(&(WIDTH as u32).to_le_bytes());
        master[12..16].copy_from_slice(&(HEIGHT as u32).to_le_bytes());
        let col=3usize;let row=1usize;
        let idx=16+((row*TILE)*WIDTH+col*TILE)*4;
        master[idx..idx+4].copy_from_slice(&[255,170,40,128]);
        let tile=tile_pixels(&master,col,row).unwrap();
        let bottom=(TILE-1)*TILE*4;
        assert_eq!(&tile[bottom..bottom+4],&[255,170,40,128]);
        assert!(tile_pixels(&master,6,0).is_none());
        assert!(tile_pixels(&master[..100],0,0).is_none());
    }
    #[test]
    fn unknown_game_textures_are_never_replaced() {
        let b=vec![128u8;4096];
        assert!(replacement(32,32,12,&b,true).is_none());
        assert!(replacement(32,32,11,&b,true).is_none());
        assert!(replacement(32,32,12,&b,false).is_none());
        assert!(replacement(32,31,12,&b,true).is_none());
    }
}
''';

def once(s:str,old:str,new:str)->str:
    if s.count(old)!=1:raise SystemExit(f"Pinned GameFrame source changed: {old[:100]!r}")
    return s.replace(old,new)

def install(root:pathlib.Path)->None:
    base=root/"wrappers"/"glide"/"src"
    lib=base/"lib.rs"
    tex=base/"render"/"textures.rs"
    module=base/"intro_logo_hd.rs"
    if not lib.is_file() or not tex.is_file() or module.exists():
        raise SystemExit("Pinned GameFrame files absent or module already installed")
    a,b=lib.read_text(encoding="utf-8"),tex.read_text(encoding="utf-8")
    a=once(a,"#[cfg(windows)]\nmod loading_image_fix;",
        "#[cfg(windows)]\nmod loading_image_fix;\n#[cfg(windows)]\nmod intro_logo_hd;")
    b=once(b,
        "                    Ok(img) => unsafe {\n"
        "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
        "                    },",
        "                    Ok(img) => unsafe {\n"
        "                        if k==0 {\n"
        "                            crate::intro_logo_hd::observe_source(lw,lh,i.format,t.ranges.len());\n"
        "                            if let Some(pixels)=crate::intro_logo_hd::replacement(lw,lh,i.format,&img.pixels,t.ranges.len()==1) {\n"
        "                                (gl.TexImage2D)(TEXTURE_2D,0,RGBA8 as i32,128,128,0,RGBA,UNSIGNED_BYTE,pixels.as_ptr().cast());\n"
        "                            } else {\n"
        "                                (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());\n"
        "                            }\n"
        "                        } else {\n"
        "                            (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());\n"
        "                        }\n"
        "                    },")
    # Only write after the entire pinned layout is validated.
    lib.write_text(a,encoding="utf-8")
    tex.write_text(b,encoding="utf-8")
    module.write_text(MODULE,encoding="utf-8")
    print("Installed exact-18-tile optional HD logo override; all other textures unchanged")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: apply_intro_logo_hd.py <pinned GameFrame>")
    install(pathlib.Path(sys.argv[1]))
