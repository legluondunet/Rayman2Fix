#!/usr/bin/env python3
"""Read-only cross-resource actor -> shared Fix.sna family/state verifier.

Requires hash-verified original Rayman 2 PC CD resources. Does not
modify or redistribute original data. The existence of a visual state
does not identify a renderer or the visible menu selection butterfly.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from inspect_rayman2_sna import decode_sna, inventory_blocks
from research_menu_cursor_actor import (
    Block, Graph, Refused, sha256, scene_blocks, decode_rtb,
    find_actors, named_actor_from_typed_links, MENU_HASH, DAT_HASH,
    RTB_OFFSET, RTB_SIZE, NAMES
)

FIX_SNA_HASH = "0a1a9a86d20cb69f978e7c897a511cdb77c2c948149669e5a34ec1c74da21147"
FIX_RTB_HASH = "fa3291add042cbfbfe0dc3cf4a43f15fae273b594506f7b8485cc51d4e3d7702"
FIX_BLOCK_SIZES = {
    (4, 0): (0x001A2200, 113740),
    (5, 1): (0x01B50020, 6894800),
    (6, 0): (0x001985B0, 10308),
    (13, 0): (0x02280020, 341872),
    (14, 0): (0x00142E78, 321868),
}
FIX_RTB_COUNTS = {(4, 0): 9687, (5, 1): 10007,
                  (6, 0): 311, (13, 0): 3207, (14, 0): 6283}


def decode_fix_blocks(source: bytes) -> dict[tuple[int,int],Block]:
    decoded = decode_sna(source)
    blocks = {}
    for info in inventory_blocks(decoded):
        if not info["data_size"]:
            continue
        key = (info["module"], info["id"])
        if key not in FIX_BLOCK_SIZES or key in blocks or (
                info["base_in_memory"], info["data_size"]) != FIX_BLOCK_SIZES[key]:
            raise Refused("unrecognized shared Fix SNA block")
        start = info["data_offset"]
        blocks[key] = Block(*key,info["base_in_memory"],
                            decoded[start:start+info["data_size"]])
    if set(blocks) != set(FIX_BLOCK_SIZES):
        raise Refused("missing shared Fix SNA block")
    return blocks


def decode_fix_rtb(source: bytes) -> dict[int,tuple[int,int,int,int]]:
    if len(source)<5 or source[0] != len(FIX_RTB_COUNTS):
        raise Refused("invalid shared Fix relocation header")
    position = 5
    relocations = {}
    observed = {}
    for unused in range(source[0]):
        if position+6>len(source):
            raise Refused("truncated shared relocation group")
        module, block, count = struct.unpack_from("<BBI",source,position)
        position += 6
        if (module,block) not in FIX_RTB_COUNTS or count>12000:
            raise Refused("unrecognized shared relocation group")
        observed[module,block] = count
        if position+8*count>len(source):
            raise Refused("truncated shared relocation entries")
        for _ in range(count):
            where, tm, tb, pad0, pad1 = struct.unpack_from(
                "<IBBBB",source,position)
            position += 8
            if where in relocations or (pad0,pad1)!=(0xCD,0xCD):
                raise Refused("invalid or repeated shared relocation")
            relocations[where] = module,block,tm,tb
    if position != len(source) or observed != FIX_RTB_COUNTS:
        raise Refused("shared relocation counts/size mismatch")
    return relocations


def linked_family_state(graph: Graph, actor: dict) -> dict:
    """Validate Raymap's Perso3dData offsets +0/+4/+8 and +0x14
    using typed relocations into the common Fix (4,0) block.
    The state list is validated independently against the family.
    """
    visual = int(actor["visual_data"],16)
    states = [graph.ptr(visual+offset,(4,1),(4,0))
              for offset in (0,4,8)]
    if states[0]!=states[1] or states[0]!=states[2]:
        raise Refused("divergent initial/current actor visual states")
    family = graph.ptr(visual+0x14,(4,1),(4,0))
    family_index = graph.u32(family+0x0C)
    if family_index != actor["type_indices"][0]:
        raise Refused("actor family index contradicts StandardGame")
    if graph.ptr(family+0x10,(4,0),(4,0)) != states[0] or (
        graph.ptr(family+0x14,(4,0),(4,0)) != states[0]):
        raise Refused("shared family state-list links do not resolve")
    if graph.u32(family+0x18) != 1:
        raise Refused("unexpected family state count")
    return {"family": f"0x{family:08X}",
            "state": f"0x{states[0]:08X}",
            "family_index":family_index,
            "visual_state_links_verified":True}


def actor_direct_object_list(graph: Graph, actor: dict) -> dict:
    """Inspect the two Perso3dData instance-object-list slots.

    Zero means NO direct list stored in these fields, *not* that the
    actor cannot produce visuals. Other actors can use animations,
    script effects or external resources. Nonzero requires two
    identically typed links to the same Menu (4,1) object list.
    """
    visual=int(actor["visual_data"],16)
    a=graph.u32(visual+0x0C)
    b=graph.u32(visual+0x10)
    if a==b==0:
        if visual+0x0C in graph.rtb or visual+0x10 in graph.rtb:
            raise Refused("zero object-list field with unexpected relocation")
        return {"direct_instance_object_list":False}
    one=graph.ptr(visual+0x0C,(4,1),(4,1))
    two=graph.ptr(visual+0x10,(4,1),(4,1))
    if a!=b or one!=two:
        raise Refused("instance object-list fields differ")
    return {"direct_instance_object_list":True}


def linked_external_graphics_block(
        graph: Graph, state: int, pointer_offset: int) -> dict:
    """Verify a fixed, typed path from a family state into the shared
    module 13/0. Its type is NOT yet known: an animation, geometry,
    resource table or renderer cannot be inferred from RTB alone.

    This is intentionally a bounded 6-hop pointer chain for the
    hash-pinned original CD, not arbitrary pointer graph exploration.
    """
    if pointer_offset not in (0x30,0x44):
        raise Refused("unapproved graphics-link field")
    origin = state + pointer_offset
    root = graph.ptr(origin,(4,0),(13,0))
    nodes = [root]
    for _ in range(5):
        following = graph.ptr(nodes[-1],(13,0),(13,0))
        if following in nodes or following<=nodes[-1]:
            raise Refused("graphics relocation chain cycles or reverses")
        nodes.append(following)
    if nodes[1] != root+0x18:
        raise Refused("unexpected module 13/0 root header")
    return {
        "state_field":f"+0x{pointer_offset:02X}",
        "shared_block":"13/0",
        "resource_root":f"0x{root:08X}",
        "typed_hops_verified":len(nodes),
        "identity_of_graphics_renderer_verified":False,
    }


def inspect_shared_links(menu_sna: Path, levels0_dat: Path,
                         fix_sna: Path, fix_rtb: Path) -> dict:
    if any(sha256(p)!=expected for p,expected in (
            (menu_sna,MENU_HASH), (levels0_dat,DAT_HASH),
            (fix_sna,FIX_SNA_HASH), (fix_rtb,FIX_RTB_HASH))):
        raise Refused("unapproved CD resource hash")
    from research_menu_cursor_actor import scene_blocks
    menu = scene_blocks(menu_sna.read_bytes())
    common = decode_fix_blocks(fix_sna.read_bytes())
    with levels0_dat.open("rb") as stream:
        stream.seek(RTB_OFFSET)
        menu_reloc=decode_rtb(stream.read(RTB_SIZE))
    common_reloc=decode_fix_rtb(fix_rtb.read_bytes())
    if set(common_reloc)&set(menu_reloc):
        raise Refused("shared and menu relocation sites overlap")
    graph=Graph({**menu,**common},{**menu_reloc,**common_reloc})
    names=menu[4,1].base
    actors=find_actors(graph)
    by_name=lambda offset: named_actor_from_typed_links(
        graph,actors,names+offset,0x38,0x3C)
    cursor=by_name(NAMES["YAM_CurseurMenu_I1"])
    genmenu=by_name(NAMES["YAM_GenMenu_I1"])
    if cursor==genmenu or (
        cursor!=named_actor_from_typed_links(
            graph,actors,names+NAMES["ARG_CurseurMenu"],0x5C,0x60)):
        raise Refused("cursor name and ARG disagree or match GenMenu")
    info_cursor=linked_family_state(graph,cursor)
    info_gen=linked_family_state(graph,genmenu)
    if info_cursor["family"]==info_gen["family"]:
        raise Refused("cursor and GenMenu family unexpectedly identical")
    cursor_gfx = linked_external_graphics_block(
        graph,int(info_cursor["state"],16),0x30)
    genmenu_gfx = linked_external_graphics_block(
        graph,int(info_gen["state"],16),0x44)
    if cursor_gfx["resource_root"]==genmenu_gfx["resource_root"]:
        raise Refused("cursor and GenMenu graphics roots overlap")
    cursor_lists=actor_direct_object_list(graph,cursor)
    genmenu_lists=actor_direct_object_list(graph,genmenu)
    direct_list_count=sum(
        int(actor_direct_object_list(graph,actor)[
            "direct_instance_object_list"]) for actor in actors)
    return {
        "mode":"hash-verified CD Menu/Fixed scene cross-links (read-only)",
        "cursor_actor":cursor["superobject"],
        "cursor_visual_data":cursor["visual_data"],
        "cursor":info_cursor,
        "genmenu_negative_control":info_gen,
        "cursor_shared_graphics_links":cursor_gfx,
        "genmenu_shared_graphics_control":genmenu_gfx,
        "cursor_instance_object_list":cursor_lists,
        "genmenu_instance_object_list":genmenu_lists,
        "actors_with_direct_instance_object_lists":direct_list_count,
        "cursor_name_and_arg_reciprocal_links_verified":True,
        "cursor_renderer_identified":False,
        "actor_visibility_modified":False
    }


def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("menu_sna","levels0_dat","fix_sna","fix_rtb"):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    try:
        result=inspect_shared_links(
            args.menu_sna,args.levels0_dat,args.fix_sna,args.fix_rtb)
    except (Refused,ValueError,OSError) as err:
        parser.exit(2,"Scene cross-link verification refused: "+str(err)+"\n")
    print(json.dumps(result,indent=2))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
