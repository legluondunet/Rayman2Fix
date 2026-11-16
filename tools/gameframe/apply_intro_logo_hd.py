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
    let mut paths=vec![cwd.join("rayman2fix").join("Rayman2Fix_logo_hd.rgba"),cwd.join("Rayman2Fix_logo_hd.rgba")];
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

/// The 128x128 atlas tile must supply EVERY GPU mip level, without
/// changing the 32x32 logical Glide source or UV coordinates.
/// Validate the entire chain before the first GL upload.
pub fn compatible_mip(w:usize,h:usize)->bool {
    w>0 && h>0 && w<=32 && h<=32 &&
    w.is_power_of_two() && h.is_power_of_two() &&
    TILE%(w*4)==0 && TILE%(h*4)==0
}

/// Premultiplied-alpha area downsampling prevents a dark edge halo
/// and yields a complete 4x mip chain: 128, 64, 32, 16, 8, 4.
pub fn mip_from_hd(base:&[u8],w_source:usize,h_source:usize)->Option<Vec<u8>> {
    if base.len()!=TILE*TILE*4 || !compatible_mip(w_source,h_source) {
        return None;
    }
    let (w,h)=(w_source*4,h_source*4);
    let (sx,sy)=(TILE/w,TILE/h);
    if sx==1 && sy==1 {return Some(base.to_vec());}
    let mut output=vec![0u8;w*h*4];
    let n=(sx*sy) as u64;
    for y in 0..h {
        for x in 0..w {
            let mut alpha=0u64;
            let mut premul=[0u64;3];
            for by in y*sy..(y+1)*sy {
                for bx in x*sx..(x+1)*sx {
                    let i=(by*TILE+bx)*4;
                    let a=base[i+3] as u64;
                    alpha+=a;
                    for c in 0..3 {premul[c]+=base[i+c] as u64*a;}
                }
            }
            let i=(y*w+x)*4;
            output[i+3]=((alpha+n/2)/n).min(255) as u8;
            if alpha!=0 {
                for c in 0..3 {
                    output[i+c]=((premul[c]+alpha/2)/alpha).min(255) as u8;
                }
            }
        }
    }
    Some(output)
}

/// The original 32x32 source mip is fingerprinted; subsequent mip
/// levels are derived from the atlas instead of the low-res game.
pub fn replacement(w:usize,h:usize,format:u32,rgba:&[u8])->Option<Vec<u8>> {
    if w!=32 || h!=32 || format!=12 || rgba.len()!=4096 {return None;}
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
    log_once(hash,&format!("tile={} hash={hash:016x} slot=({}, {}) original=32x32 gpu=128x128 complete_mip_chain=1",idx+17,col,row));
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
    fn original_32_to_1_mips_generate_a_complete_hd_pyramid() {
        let source=vec![240u8,170u8,30u8,255u8].repeat(TILE*TILE);
        for size in [32,16,8,4,2,1] {
            assert!(compatible_mip(size,size));
            let pixels=mip_from_hd(&source,size,size).unwrap();
            assert_eq!(pixels.len(),size*size*16*4);
            assert!(pixels.chunks_exact(4).all(|p|p==[240,170,30,255]));
        }
        for invalid in [0,3,33,64] {
            assert!(!compatible_mip(invalid,invalid));
            assert!(mip_from_hd(&source,invalid,invalid).is_none());
        }
        assert!(mip_from_hd(&source[..500],16,16).is_none());
    }
    #[test]
    fn alpha_downsample_preserves_color_at_transparent_edges() {
        let mut source=vec![0u8;TILE*TILE*4];
        source[..4].copy_from_slice(&[255,180,30,255]);
        let half=mip_from_hd(&source,16,16).unwrap();
        assert_eq!(&half[..4],&[255,180,30,64]);
        assert_eq!(&half[4..8],&[0,0,0,0]);
    }
    #[test]
    fn unknown_game_textures_are_never_replaced() {
        let b=vec![128u8;4096];
        assert!(replacement(32,32,12,&b).is_none());
        assert!(replacement(32,32,11,&b).is_none());
        assert!(replacement(32,31,12,&b).is_none());
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
        "            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {",
        """            // Check every mip before the first replacement upload.
            let hd_chain_ok=w==32 && h==32 && i.format==12 && levels.clone().all(|lod| {
                size(lod,i.aspect).map_or(false,|(mw,mh)|crate::intro_logo_hd::compatible_mip(mw,mh))
            });
            let mut hd_tile:Option<Vec<u8>>=None;
            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {""")
    b=once(b,
        "                    Ok(img) => unsafe {\n"
        "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
        "                    },",
        """                    Ok(img) => unsafe {
                        if k==0 {
                            crate::intro_logo_hd::observe_source(lw,lh,i.format,t.ranges.len());
                            if hd_chain_ok {
                                hd_tile=crate::intro_logo_hd::replacement(lw,lh,i.format,&img.pixels);
                            }
                        }
                        if let Some(ref base)=hd_tile {
                            if let Some(pixels)=crate::intro_logo_hd::mip_from_hd(base,lw,lh) {
                                (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,(lw*4) as i32,(lh*4) as i32,0,RGBA,UNSIGNED_BYTE,pixels.as_ptr().cast());
                            } else {
                                (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());
                            }
                        } else {
                            (gl.TexImage2D)(TEXTURE_2D,k as i32,RGBA8 as i32,lw as i32,lh as i32,0,RGBA,UNSIGNED_BYTE,img.pixels.as_ptr().cast());
                        }
                    },""")
    # Only write after the entire pinned layout is validated.
    lib.write_text(a,encoding="utf-8")
    tex.write_text(b,encoding="utf-8")
    module.write_text(MODULE,encoding="utf-8")
    print("Installed exact-18-tile optional HD logo override; all other textures unchanged")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: apply_intro_logo_hd.py <pinned GameFrame>")
    install(pathlib.Path(sys.argv[1]))
