"""Synthetic-only checks: never bundle or decode proprietary game assets."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from research_menu_cursor_actor import (
    Block,Graph,Refused,RTB_SIZE,decode_rtb,find_actors,
    scene_blocks,next_mask,inspect,
)
from inspect_rayman2_sna import decode_sna

MAGIC=bytes.fromhex("79ccb56a")


class SceneActorResearchTests(unittest.TestCase):
    def test_static_masks_and_bad_rtb_rejected(self):
        self.assertEqual(next_mask(0xFF000000), 0x78a7bc31)
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
