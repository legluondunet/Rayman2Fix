#!/usr/bin/env python3
"""Offline, read-only actor graph for the hash-verified Rayman 2 CD Menu.

Requires the owner's Menu.sna and levels0.dat. Emits ONLY pointer/type
metadata, never original decrypted data. No executable, asset or memory
patching. A candidate actor is not proof of its live renderer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from inspect_rayman2_sna import decode_sna, inventory_blocks

MENU_HASH = "73e754887d85954ee611b6fbff118293fa9f264f5f165c3e9b2823176998ecbb"
DAT_HASH = "8a9a1dc7118b31c9d63fdb901889a073f23641a5d0933d3af839e0df71e82b17"
RTB_OFFSET = 0xE65000  # valid only for the verified LEVELS0.DAT
RTB_SIZE = 68203
COUNTS = {(4, 1): 1600, (5, 2): 5325, (13, 1): 1597}
NAMES = {"YAM_CurseurMenu_I1": 444, "ARG_CurseurMenu": 536,
         "YAM_GenMenu_I1": 2704}


class Refused(ValueError):
    """No conclusion is permitted from unverified source data."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for piece in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(piece)
    return digest.hexdigest()


def next_mask(mask: int) -> int:
    x = mask ^ 0x075BD924
    return (16807*x - 0x7FFFFFFF*(x//0x1F31D)) & 0xFFFFFFFF


def decode_rtb(raw: bytes) -> dict[int, tuple[int,int,int,int]]:
    if len(raw) != RTB_SIZE:
        raise Refused("RTB length")
    mask = next_mask(0xFF000000)  # level=0, type=0, byte2=0, byte3=FF
    data = bytearray(raw)
    for i in range(len(data)):
        data[i] ^= (mask >> 8) & 255
        mask = next_mask(mask)
    offset = 4  # DAT table's 4-byte prefix
    groups = data[offset]
    offset += 5
    if groups != 3:
        raise Refused("RTB group count")
    result = {}
    observed = {}
    for _ in range(groups):
        if offset+6 > len(data):
            raise Refused("truncated RTB group")
        module, block, count = struct.unpack_from("<BBI", data, offset)
        offset += 6
        observed[module,block] = count
        if count > 9000 or offset + 8*count > len(data):
            raise Refused("RTB entry bounds")
        for _ in range(count):
            source, tm, tb, pad0, pad1 = struct.unpack_from(
                "<IBBBB", data, offset)
            offset += 8
            if source in result or (pad0,pad1) != (0xCD,0xCD):
                raise Refused("ambiguous RTB source")
            result[source] = (module,block,tm,tb)
    if observed != COUNTS or offset != len(data):
        raise Refused("unexpected RTB layout")
    return result


@dataclass(frozen=True)
class Block:
    module: int
    index: int
    base: int
    data: bytes

    def has(self, addr: int, size: int=4) -> bool:
        return self.base <= addr and size >= 0 and (
            addr-self.base <= len(self.data)-size)

    def u32(self, addr: int) -> int:
        if not self.has(addr):
            raise Refused("scene address outside module")
        return struct.unpack_from("<I",self.data,addr-self.base)[0]


def scene_blocks(encoded_sna: bytes) -> dict[tuple[int,int], Block]:
    decoded = decode_sna(encoded_sna)
    out = {}
    for entry in inventory_blocks(decoded):
        if not entry["data_size"]:
            continue
        k = entry["module"],entry["id"]
        if k in out or entry["base_in_memory"] < 0x10000:
            raise Refused("ambiguous SNA module")
        start = entry["data_offset"]
        size = entry["data_size"]
        out[k] = Block(*k,entry["base_in_memory"],decoded[start:start+size])
    if set(out) != set(COUNTS):
        raise Refused("different SNA modules")
    return out


class Graph:
    def __init__(self, blocks, rtb):
        self.blocks, self.rtb = blocks, rtb

    def block(self, addr: int, size: int=4) -> Block:
        matches = [b for b in self.blocks.values() if b.has(addr,size)]
        if len(matches) != 1:
            raise Refused("unmapped or ambiguous scene address")
        return matches[0]

    def u32(self, addr: int) -> int:
        return self.block(addr).u32(addr)

    def ptr(self, source: int, src: tuple[int,int],
            dest: tuple[int,int]) -> int:
        if self.rtb.get(source) != (*src,*dest):
            raise Refused("typed relocation absent or wrong")
        target = self.u32(source)
        b = self.block(target)
        if (b.module,b.index) != dest:
            raise Refused("typed target mismatch")
        return target


def find_actors(graph: Graph) -> list[dict]:
    object_block = graph.blocks[5,2]
    found = []
    for delta in range(0,len(object_block.data)-52,4):
        so = object_block.base+delta
        if graph.u32(so) != 2:  # Rayman 2 SO type: Perso
            continue
        try:
            p = graph.ptr(so+4,(5,2),(4,1))
            std = graph.ptr(p+4,(4,1),(4,1))
            if graph.ptr(std+12,(4,1),(5,2)) != so:
                continue
            brain = graph.ptr(p+12,(4,1),(4,1))
            visual = graph.ptr(p,(4,1),(4,1))
            ids = [graph.u32(std+i*4) for i in range(3)]
            if any(v>2000 for v in ids):
                continue
        except Refused:
            continue
        found.append({
            "superobject":f"0x{so:08X}", "perso":f"0x{p:08X}",
            "standard_game":f"0x{std:08X}", "brain":f"0x{brain:08X}",
            "visual_data":f"0x{visual:08X}", "type_indices":ids,
            "so_flags":f"0x{graph.u32(so+0x30):08X}",
            "draw_flags_raw":f"0x{graph.u32(so+0x2C):08X}"})
    return found



def named_actor_from_typed_links(graph: Graph, actors: list[dict],
                                 name_address: int,
                                 record_to_std_offset: int,
                                 std_to_record_offset: int) -> dict:
    """Find the unique actor owning a scene-name record, without guessing
    its role from a numerical family/model/instance tuple.

    Both directions MUST have validated RTB entries:
      (name - 24) -> (StandardGame + record_to_std_offset)
      (StandardGame + std_to_record_offset) -> (name - 24)
    The -12 self-pointer is checked independently by inspect().
    """
    record = name_address - 24
    target = graph.ptr(record, (4, 1), (4, 1))
    matches = []
    for actor in actors:
        std = int(actor["standard_game"], 16)
        if target != std + record_to_std_offset:
            continue
        try:
            backlink = graph.ptr(std + std_to_record_offset,
                                 (4, 1), (4, 1))
        except Refused:
            continue
        if backlink == record:
            matches.append(actor)
    if len(matches) != 1:
        raise Refused("name-to-actor RTB links absent or ambiguous")
    return matches[0]


def inspect(menu: Path, dat: Path) -> dict:
    if sha256(menu) != MENU_HASH or sha256(dat) != DAT_HASH:
        raise Refused("unapproved resource hashes")
    blocks = scene_blocks(menu.read_bytes())
    with dat.open("rb") as stream:
        stream.seek(RTB_OFFSET)
        relocations = decode_rtb(stream.read(RTB_SIZE))
    graph = Graph(blocks,relocations)
    names = blocks[4,1]
    for text,rel in NAMES.items():
        encoded = text.encode("ascii")+b"\x00"
        if names.data[rel:rel+len(encoded)] != encoded:
            raise Refused("name table fingerprint mismatch")
        if graph.ptr(names.base+rel-12,(4,1),(4,1)) != names.base+rel:
            raise Refused("missing name self-reference")
    actors = find_actors(graph)
    cursor_name = names.base + NAMES["YAM_CurseurMenu_I1"]
    arg_name = names.base + NAMES["ARG_CurseurMenu"]
    genmenu_name = names.base + NAMES["YAM_GenMenu_I1"]
    # Two DIFFERENT native name records independently point back to the
    # cursor actor's StandardGame fields (+0x38/+0x5C), with matching
    # reciprocal links (+0x3C/+0x60). GenMenu is a negative control.
    cursor_by_name = named_actor_from_typed_links(
        graph, actors, cursor_name, 0x38, 0x3C)
    cursor_by_arg = named_actor_from_typed_links(
        graph, actors, arg_name, 0x5C, 0x60)
    if cursor_by_arg != cursor_by_name:
        raise Refused("cursor and ARG name records disagree")
    genmenu_by_name = named_actor_from_typed_links(
        graph, actors, genmenu_name, 0x38, 0x3C)
    if genmenu_by_name == cursor_by_name:
        raise Refused("cursor and GenMenu actor identities overlap")

    def only(types):
        result = [a for a in actors if a["type_indices"]==list(types)]
        if len(result)!=1:
            raise Refused("actor type tuple absent or ambiguous")
        return result[0]

    # Numerical type tuples are cross-checks, not identity sources.
    if cursor_by_name != only((2,11,11)) or genmenu_by_name != only((3,13,13)):
        raise Refused("name-linked actor disagrees with type tuple")

    return {
        "mode":"hash-verified, read-only static SNA/RTB",
        "relocations_verified":len(relocations),
        "perso_chains_verified":len(actors),
        "cursor_name_anchor_verified":True,
        "cursor_candidate":cursor_by_name,
        "cursor_name_links_verified":True,
        "cursor_arg_links_verified":True,
        "video_controller_control":only((3,12,12)),
        "genmenu_control":genmenu_by_name,
        "genmenu_name_links_verified":True,
        "actor_to_renderer_verified":False,
        "asset_modified":False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("menu_sna",type=Path)
    parser.add_argument("levels0_dat",type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.menu_sna,args.levels0_dat),indent=2))
    except (Refused,ValueError,OSError) as err:
        parser.exit(2,"Scene inspection refused: "+str(err)+"\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
