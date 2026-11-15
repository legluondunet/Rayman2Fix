#!/usr/bin/env python3
"""Create optional Rayman2Fix 6x3 RGBA atlas from user's own HD logo PNG.

Usage:
    python prepare_intro_logo_hd.py --download
    python prepare_intro_logo_hd.py --png path/to/transparent_logo.png

The default RayWiki logo is linked as a reference but no image is
redistributed or committed to the Rayman2Fix executable/repository.
Requires Pillow (on Manjaro: sudo pacman -S python-pillow).
"""
from __future__ import annotations
import argparse
import hashlib
import io
import pathlib
import struct
import urllib.request
import urllib.error

SOURCE_URL="https://raymanpc.com/wiki/script-en/images/b/bb/Rayman_2_Logo.png"
SOURCE_PAGE="https://raymanpc.com/wiki/script-en/index.php?title=File:Rayman_2_Logo.png"

def find_local_logo(home:pathlib.Path, cwd:pathlib.Path) -> pathlib.Path | None:
    """Prefer a logo saved in the user's browser over another HTTP request."""
    names=("Rayman_2_Logo.png","Rayman 2 Logo.png")
    for directory in (cwd, home/"Téléchargements", home/"Downloads"):
        for name in names:
            candidate=directory/name
            if candidate.is_file():
                return candidate
    return None

MAGIC=b"R2LOGOHD"
WIDTH,HEIGHT=768,384
MARGIN_X,MARGIN_Y=16,12
INNER_WIDTH,INNER_HEIGHT=736,360

def convert(image, output:pathlib.Path) -> None:
    from PIL import Image
    im=image.convert("RGBA")
    alpha=im.getchannel("A")
    if alpha.getextrema()==(255,255):
        raise ValueError("A transparent-background logo PNG is required (the supplied image is fully opaque)")
    bbox=alpha.getbbox()
    if bbox is None:
        raise ValueError("The supplied logo is fully transparent")
    foreground=im.crop(bbox)
    resampling=getattr(getattr(Image,"Resampling",Image),"LANCZOS")
    foreground=foreground.resize((INNER_WIDTH,INNER_HEIGHT),resample=resampling)
    atlas=Image.new("RGBA",(WIDTH,HEIGHT),(0,0,0,0))
    atlas.alpha_composite(foreground,(MARGIN_X,MARGIN_Y))
    data=MAGIC+struct.pack("<II",WIDTH,HEIGHT)+atlas.tobytes()
    assert len(data)==16+WIDTH*HEIGHT*4
    output.write_bytes(data)
    print(f"Created {output} ({WIDTH}x{HEIGHT}, {len(data)} bytes, SHA256 {hashlib.sha256(data).hexdigest()})")
    print("Place this atlas beside Rayman2Fix.exe; remove it to restore the original game logo.")
    print("The logo is a user-provided optional local resource and is NOT included in the launcher.")

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    source=p.add_mutually_exclusive_group(required=True)
    source.add_argument("--png",type=pathlib.Path,help="A high-resolution transparent logo PNG supplied by the user")
    source.add_argument("--download",action="store_true",help="Download the RayWiki logo for personal testing")
    p.add_argument("--output",type=pathlib.Path,default=pathlib.Path("Rayman2Fix_logo_hd.rgba"))
    args=p.parse_args()
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("Pillow is required (Manjaro: sudo pacman -S python-pillow)") from exc
    if args.download:
        local=find_local_logo(pathlib.Path.home(),pathlib.Path.cwd())
        if local is not None:
            print(f"Using browser-downloaded logo: {local}")
            image=Image.open(local)
            image.load()
        else:
            try:
                req=urllib.request.Request(SOURCE_URL,headers={
                    "User-Agent":"Mozilla/5.0 (X11; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0",
                    "Referer":SOURCE_PAGE,
                    "Accept":"image/png,image/*;q=0.9,*/*;q=0.5",
                })
                with urllib.request.urlopen(req,timeout=30) as res:
                    data=res.read(8*1024*1024+1)
                if len(data)>8*1024*1024:
                    raise ValueError("Image download is unexpectedly large")
                image=Image.open(io.BytesIO(data))
                image.load()
            except (urllib.error.HTTPError,urllib.error.URLError,ValueError,OSError) as exc:
                raise SystemExit(
                    f"RayWiki refused automated download ({exc}).\n"
                    f"Open this full-resolution PNG in your browser:\n{SOURCE_URL}\n"
                    "Save as Rayman_2_Logo.png beside the launcher or in Downloads/Téléchargements.\n"
                    "Then rerun: python prepare_intro_logo_hd.py --download\n"
                    "Or use: python prepare_intro_logo_hd.py --png /path/to/logo.png"
                ) from exc
    else:
        image=Image.open(args.png)
        image.load()
    convert(image,args.output)

if __name__=="__main__":main()
