#!/usr/bin/env python3
"""Offline, read-only Rayman 2 Fix.sna / Fix.rtb / Menu.dsb text inventory.

Original game files are never written or embedded in the repository.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from inspect_rayman2_sna import decode_sna, inventory_blocks

LOCALES={"/C:LANGUAGE":"en", "/C:LANGUE":"fr",
         "/C:IDIOMA":"es", "/C:LINGUA":"it", "/C:SPRACHE":"de"}
HANDLES=(370,371,372,373,377,378,379,380,381,382,383,384,385)


class InvalidMenuData(ValueError):
    pass


def inspect(sna, rtb, dsb):
    decoded=decode_sna(sna)
    blocks=inventory_blocks(decoded)
    mapping={(b["module"],b["id"]):b for b in blocks if b["data_size"]}
    if len(rtb)<5 or not 0<rtb[0]<=63:
        raise InvalidMenuData("invalid RTB header")
    offset=5
    count_total=0
    tables=[]
    counts=[]
    for _ in range(rtb[0]):
        if offset+6>len(rtb):
            raise InvalidMenuData("truncated RTB group")
        module,ident,n=struct.unpack_from("<BBI",rtb,offset)
        offset+=6
        count_total+=n
        if count_total>250000 or n>(len(rtb)-offset)//8:
            raise InvalidMenuData("RTB group exceeds file")
        key=(module,ident)
        if key not in mapping:
            raise InvalidMenuData("RTB group missing SNA payload")
        block=mapping[key]
        run=[];previous=None
        for i in range(n):
            loc,tmod,tid,b6,b7=struct.unpack_from("<IBBBB",rtb,offset+8*i)
            delta=loc-block["base_in_memory"]
            if delta<0 or delta+4>block["data_size"]:
                raise InvalidMenuData("RTB slot outside SNA payload")
            if key==(14,0):
                if previous is None or loc!=previous+4:
                    if len(run)==666:
                        tables.append(run)
                    run=[]
                ptr=struct.unpack_from("<I",decoded,block["data_offset"]+delta)[0]
                run.append((loc,ptr))
                previous=loc
        if key==(14,0) and len(run)==666:
            tables.append(run)
        offset+=8*n
        counts.append(dict(module=module,id=ident,relocations=n))
    if offset!=len(rtb) or len(tables)!=5 or (14,0) not in mapping:
        raise InvalidMenuData("missing five 666-entry language pointer tables")
    text_block=mapping[(14,0)]
    languages={}
    for table in tables:
        def string_at(handle):
            pos=text_block["data_offset"]+table[handle][1]-text_block["base_in_memory"]
            end_limit=text_block["data_offset"]+text_block["data_size"]
            if not text_block["data_offset"]<=pos<end_limit:
                raise InvalidMenuData("localized text outside module 14")
            end=decoded.find(b"\0",pos,min(pos+4096,end_limit))
            if end<0:
                raise InvalidMenuData("unterminated localized text")
            return decoded[pos:end].decode("cp1252")
        locale=LOCALES.get(string_at(370))
        if not locale or locale in languages:
            raise InvalidMenuData("unknown or duplicated locale")
        languages[locale]=dict(entries=666,table_va="0x%08x"%table[0][0],
                               handles={str(i):string_at(i) for i in HANDLES})
    dd=decode_sna(dsb)
    if len(dd)<48 or struct.unpack_from("<I",dd,4)[0]!=0:
        raise InvalidMenuData("unexpected DSB allocation section")
    return dict(files={name:dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
                       for name,data in (("fix.sna",sna),("fix.rtb",rtb),("menu.dsb",dsb))},
                sna_blocks=len(blocks),
                sna_nonempty=sum(b["data_size"]>0 for b in blocks),
                rtb_groups=counts,rtb_relocations=count_total,
                languages=languages,
                menu_dsb_next_section=struct.unpack_from("<I",dd,44)[0],
                menu_dsb_bitmap="Rayman.bmp" if b"Rayman.bmp\0" in dd else None,
                resolution_submenu_identified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("fix_sna","fix_rtb","menu_dsb"):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    try:
        report=inspect(args.fix_sna.read_bytes(),args.fix_rtb.read_bytes(),
                       args.menu_dsb.read_bytes())
    except (OSError,InvalidMenuData,ValueError) as exc:
        parser.exit(2,"Menu resource inventory failed: %s\n"%exc)
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=="__main__":
    main()
