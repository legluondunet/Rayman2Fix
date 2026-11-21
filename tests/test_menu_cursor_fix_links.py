"""Synthetic, asset-free tests for Rayman 2 shared Fix -> Menu links."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from research_menu_cursor_actor import Block,Graph,Refused
from research_menu_cursor_fix_links import (
    linked_family_state, decode_fix_rtb, decode_fix_blocks
)


class SharedCursorVisualTests(unittest.TestCase):
    def make_graph(self):
        m4,f4 = 0x07000000,0x001A0000
        scene=bytearray(0x100)
        shared=bytearray(0x300)
        visual=m4+0x20
        state=f4+0x180
        family=f4+0x80
        rt={}
        for offset in (0,4,8):
            struct.pack_into("<I",scene,0x20+offset,state)
            rt[visual+offset]=(4,1,4,0)
        struct.pack_into("<I",scene,0x20+0x14,family)
        rt[visual+0x14]=(4,1,4,0)
        struct.pack_into("<I",shared,0x80+0x0C,2)
        for offset in (0x10,0x14):
            struct.pack_into("<I",shared,0x80+offset,state)
            rt[family+offset]=(4,0,4,0)
        struct.pack_into("<I",shared,0x80+0x18,1)
        blocks={(4,1):Block(4,1,m4,bytes(scene)),
                (4,0):Block(4,0,f4,bytes(shared))}
        actor={"visual_data":f"0x{visual:08X}",
               "type_indices":[2,11,11]}
        return blocks,rt,actor

    def test_verified_cross_resource_state_and_family(self):
        blocks,rt,actor=self.make_graph()
        result=linked_family_state(Graph(blocks,rt),actor)
        self.assertEqual(result["family_index"],2)
        self.assertEqual(result["family"],"0x001A0080")
        self.assertEqual(result["state"],"0x001A0180")
        self.assertTrue(result["visual_state_links_verified"])

    def test_missing_relocations_or_wrong_family_refused(self):
        blocks,rt,actor=self.make_graph()
        modified=dict(rt)
        modified.pop(0x07000020+0x14)
        with self.assertRaises(Refused):
            linked_family_state(Graph(blocks,modified),actor)
        modified=dict(rt)
        modified.pop(0x001A0080+0x10)
        with self.assertRaises(Refused):
            linked_family_state(Graph(blocks,modified),actor)
        actor_bad={**actor,"type_indices":[3,11,11]}
        with self.assertRaises(Refused):
            linked_family_state(Graph(blocks,rt),actor_bad)

    def test_malformed_common_resources_refused(self):
        with self.assertRaises(Refused):
            decode_fix_rtb(bytes(50))
        with self.assertRaises((Refused,ValueError)):
            decode_fix_blocks(bytes.fromhex("79ccb56a"))


if __name__=="__main__":
    unittest.main()
