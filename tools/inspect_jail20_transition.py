#!/usr/bin/env python3
"""Read-only PC SNA local actor/rule audit; no decoded assets are emitted."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from inspect_rayman2_sna import decode_sna, inventory_blocks


def inspect(source):
    decoded = decode_sna(source)
    blocks = {(b['module'], b['id']): (b['base_in_memory'] & 0xffffffff,
              memoryview(decoded)[b['data_offset']:b['data_offset']+b['data_size']])
              for b in inventory_blocks(decoded) if b['data_size']}
    base, data = blocks[4, 1]

    def locate(address, size=4):
        matches = [(key, address-a) for key, (a, x) in blocks.items()
                   if a <= address and address-a+size <= len(x)]
        if len(matches) != 1:
            raise ValueError('unresolved or ambiguous local pointer')
        return matches[0]

    def read(address, size=4):
        key, off = locate(address, size)
        return blocks[key][1][off:off+size]

    def word(address):
        return struct.unpack('<I', read(address))[0]

    def rel(address):
        key, off = locate(address)
        return [*key, off]

    actors = []
    for off in range(0, len(data)-0x24, 4):
        actor = base+off
        try:
            std = word(actor+4)
            so = word(std+12)
            if word(so) != 2 or word(so+4) != actor:
                continue
            mind = word(word(actor+12))
            model = word(mind)
            count = word(word(model)+4)
            if not 0 < count <= 100:
                continue
            actors.append((actor, std, so, mind, model))
        except ValueError:
            continue

    scripts = set()
    failures = 0
    maps = []
    writes = []
    for actor, std, so, mind, model in actors:
        for kind in (0, 4):
            try:
                desc = word(model+kind)
                if not desc:
                    continue
                table, count = word(desc), word(desc+4)
                if count > 100:
                    raise ValueError('behaviour cap')
                for behaviour in range(count):
                    entry = table+12*behaviour
                    rules, schedule = word(entry), word(entry+4)
                    n = read(entry+8, 1)[0]
                    if n > 64:
                        raise ValueError('rule cap')
                    slots = [rules+4*j for j in range(n)] + ([schedule] if schedule else [])
                    for rule, slot in enumerate(slots):
                        try:
                            start = word(slot)
                            nodes = []
                            for j in range(8192):
                                raw = read(start+8*j, 8)
                                param = struct.unpack_from('<I', raw)[0]
                                depth, typ = raw[6], raw[7]
                                if typ > 40 or depth > 63 or (j == 0 and depth != 1):
                                    raise ValueError('node shape')
                                nodes.append((param, depth, typ))
                                if depth == 0:
                                    break
                            if not nodes or nodes[-1][1:] != (0, 8):
                                raise ValueError('missing EndTree')
                            scripts.add(start)
                            for j, (param, depth, typ) in enumerate(nodes[:-1]):
                                arg, argdepth, argtype = nodes[j+1]
                                if argdepth != depth+1:
                                    continue
                                site = dict(actor=rel(actor), behaviour=behaviour,
                                            rule=rule, node=j, reflex=kind == 4)
                                if typ == 4 and param == 86 and argtype == 20:
                                    try:
                                        name = bytes(read(arg, 9)).lower()
                                    except ValueError:
                                        name = b''
                                    if name == b'learn_10\x00':
                                        maps.append(dict(site, target='learn_10',
                                                         actor_type_ids=[word(std+4*k) for k in range(3)],
                                                         initial_max_hp=read(std+0x22, 1)[0],
                                                         superobject=rel(so)))
                                if not ((typ == 4 and 6 <= param <= 10) or (typ == 3 and param in (47, 48))):
                                    continue
                                target = None
                                resolution = 'unresolved'
                                if argtype == 0 and arg == 11:
                                    target, resolution = so, 'self'
                                elif argtype == 23:
                                    target, resolution = arg, 'literal'
                                elif argtype == 11:
                                    try:
                                        mem = word(mind+12)
                                        decl = word(word(mem))
                                        info = word(decl+4)
                                        if arg < read(decl+12, 1)[0] and word(info+16*arg+4) == 9:
                                            target = word(word(mem+8)+word(info+16*arg))
                                            resolution = 'stored-actor-var'
                                    except ValueError:
                                        pass
                                try:
                                    target_rel = rel(target) if target else None
                                except ValueError:
                                    target_rel = None
                                writes.append(dict(site, opcode=param, node_type=typ,
                                                   resolution=resolution, target=target_rel))
                        except ValueError:
                            failures += 1
            except ValueError:
                failures += 1
    targets = {tuple(m['superobject']) for m in maps}
    return dict(sha256=hashlib.sha256(source).hexdigest(), actors=len(actors),
                distinct_scripts=len(scripts), scan_failures=failures, map_calls=maps,
                max_hp_writer_sites=len(writes),
                writers_to_map_owner=[w for w in writes if w['target'] and tuple(w['target']) in targets],
                unresolved_writer_sites=sum(w['target'] is None for w in writes),
                limits='Local validated actors and normal/reflex rules only; no macros, RTB, dynamic assignments or live execution. Stored variable values are not runtime proof.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sna', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.sna.read_bytes()), indent=2))
