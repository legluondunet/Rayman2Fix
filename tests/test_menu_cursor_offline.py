"""Synthetic-only checks: never bundle or decode proprietary game assets."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from research_menu_cursor_actor import (
    Block,Graph,Refused,RTB_SIZE,decode_rtb,find_actors,
    scene_blocks,next_mask,inspect,named_actor_from_typed_links,
)
from inspect_rayman2_sna import decode_sna

MAGIC=bytes.fromhex("79ccb56a")


class SceneActorResearchTests(unittest.TestCase):
    def test_static_masks_and_bad_rtb_rejected(self):
        self.assertEqual(next_mask(0xFF000000), 0x560D49DE)
        with self.assertRaises(Refused):
            decode_rtb(b"")
        with self.assertRaises(Refused):
            decode_rtb(bytes(RTB_SIZE))

    def test_typed_superobject_perso_standardgame_chain(self):
        # Minimal, totally synthetic x86 scene with three independent
        # structures and bidirectional typed relocations.
        m4=0x07000000
        m5=0x08000000
        object_data=bytearray(0x100)
        scene_data=bytearray(0x400)
        reloc={}
        for i,types in enumerate(((2,11,11),(3,12,12),(3,13,13))):
            so=m5+i*0x50
            perso=m4+i*0x100
            std=perso+0x40
            brain=perso+0x80
            visual=perso+0x90
            struct.pack_into("<II",object_data,i*0x50,2,perso)
            struct.pack_into("<IIII",scene_data,i*0x100,visual,std,0,brain)
            struct.pack_into("<IIII",scene_data,i*0x100+0x40,*types,so)
            struct.pack_into("<II",object_data,i*0x50+0x2C,0xFFFFFFFF,i)
            for source,target in (
                (so+4,(5,2,4,1)),(perso,(4,1,4,1)),
                (perso+4,(4,1,4,1)),(perso+12,(4,1,4,1)),
                (std+12,(4,1,5,2))):
                reloc[source]=target
        g=Graph({(4,1):Block(4,1,m4,bytes(scene_data)),
                 (5,2):Block(5,2,m5,bytes(object_data))},reloc)
        result=find_actors(g)
        self.assertEqual([a["type_indices"] for a in result],
                         [[2,11,11],[3,12,12],[3,13,13]])
        self.assertEqual(result[0]["superobject"],"0x08000000")
        self.assertEqual(result[0]["so_flags"],"0x00000000")
        self.assertEqual(result[1]["so_flags"],"0x00000001")
        with self.assertRaises(Refused):
            g.ptr(m5+4,(5,2),(5,2))  # wrong destination
        with self.assertRaises(Refused):
            g.ptr(m5+999,(5,2),(4,1))  # unknown source
        bad=dict(reloc)
        bad.pop(m4+0x40+12)
        rejected=Graph(g.blocks,bad)
        self.assertEqual(len(find_actors(rejected)),2)

    def test_bidirectional_named_actor_links_and_fail_closed(self):
        # All memory addresses in this test are synthetic. Name records
        # link to StdGame, which independently links back to the records.
        m4,m5=0x07000000,0x08000000
        data=bytearray(0x700)
        objects=bytearray(0x200)
        rtb={}
        actors=[]
        for i,(family,model,instance) in enumerate(
                ((2,11,11),(3,12,12),(3,13,13))):
            so=m5+i*0x50
            perso=m4+i*0x100
            std=perso+0x40
            brain=perso+0x80
            vis=perso+0x90
            struct.pack_into("<II",objects,i*0x50,2,perso)
            struct.pack_into("<IIII",data,i*0x100,vis,std,0,brain)
            struct.pack_into("<IIII",data,i*0x100+0x40,
                             family,model,instance,so)
            for site,source,target in (
                (so+4,(5,2),(4,1)),
                (perso,(4,1),(4,1)),
                (perso+4,(4,1),(4,1)),
                (perso+12,(4,1),(4,1)),
                (std+12,(4,1),(5,2))):
                rtb[site]=(*source,*target)
            actors.append((so,std))
        cursor_record=m4+0x408
        arg_record=m4+0x448
        genmenu_record=m4+0x488
        # owner_index, record, forward field, reciprocal field
        for i,record,fwd,back in (
                (0,cursor_record,0x38,0x3C),
                (0,arg_record,0x5C,0x60),
                (2,genmenu_record,0x38,0x3C)):
            std=actors[i][1]
            struct.pack_into("<I",data,record-m4,std+fwd)
            struct.pack_into("<I",data,std+back-m4,record)
            rtb[record]=(4,1,4,1)
            rtb[std+back]=(4,1,4,1)
        blocks={(4,1):Block(4,1,m4,bytes(data)),
                (5,2):Block(5,2,m5,bytes(objects))}
        g=Graph(blocks,rtb)
        found=find_actors(g)
        cursor=named_actor_from_typed_links(
            g,found,cursor_record+24,0x38,0x3C)
        self.assertEqual(cursor["standard_game"],f"0x{actors[0][1]:08X}")
        self.assertEqual(cursor,named_actor_from_typed_links(
            g,found,arg_record+24,0x5C,0x60))
        genmenu=named_actor_from_typed_links(
            g,found,genmenu_record+24,0x38,0x3C)
        self.assertNotEqual(cursor,genmenu)

        # A missing RTB link is not a valid guessed pointer, even
        # if the 32-bit values in the game memory appear to match.
        no_back=dict(rtb)
        no_back.pop(actors[0][1]+0x3C)
        with self.assertRaises(Refused):
            named_actor_from_typed_links(
                Graph(blocks,no_back),found,cursor_record+24,0x38,0x3C)
        # A typed link to a different plausible record must be refused.
        different=bytearray(data)
        struct.pack_into("<I",different,actors[0][1]+0x60-m4,
                         cursor_record)
        with self.assertRaises(Refused):
            named_actor_from_typed_links(
                Graph({**blocks,(4,1):Block(4,1,m4,bytes(different))},
                      rtb),found,arg_record+24,0x5C,0x60)
        # An array with two equally valid actor identities must fail
        # closed; we never decide from actor scan order.
        with self.assertRaises(Refused):
            named_actor_from_typed_links(
                g,found+[cursor],cursor_record+24,0x38,0x3C)

    def test_file_hash_gate_refuses_unrecognized_input(self):
        # A new edition must be explicitly studied and approved.
        from tempfile import TemporaryDirectory
        with TemporaryDirectory() as tmp:
            a=Path(tmp)/"menu.sna"
            b=Path(tmp)/"levels0.dat"
            a.write_bytes(MAGIC+b"x")
            b.write_bytes(b"123")
            with self.assertRaises(Refused):
                inspect(a,b)


if __name__=="__main__":
    unittest.main()
