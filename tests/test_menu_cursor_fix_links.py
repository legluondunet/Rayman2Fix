"""Synthetic, asset-free tests for Rayman 2 shared Fix -> Menu links."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from research_menu_cursor_actor import Block,Graph,Refused
from research_menu_cursor_fix_links import (
    linked_family_state, linked_external_graphics_block,
    actor_direct_object_list, decode_fix_rtb, decode_fix_blocks
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

    def test_bounded_graphics_chain_and_negative_controls(self):
        m4,f4,m13=0x07000000,0x001A0000,0x02280000
        shared=bytearray(0x400)
        other=bytearray(0x500)
        cursor_state=f4+0x80
        genmenu_state=f4+0x180
        rt={}
        nodes_a=[m13+0x20,m13+0x38,m13+0x68,
                 m13+0x7c,m13+0x94,m13+0xa0]
        nodes_b=[m13+0x120,m13+0x138,m13+0x170,
                 m13+0x184,m13+0x198,m13+0x1a4]
        for state,field,nodes in (
                (cursor_state,0x30,nodes_a),
                (genmenu_state,0x44,nodes_b)):
            struct.pack_into("<I",shared,state+field-f4,nodes[0])
            rt[state+field]=(4,0,13,0)
            for one,two in zip(nodes,nodes[1:]):
                struct.pack_into("<I",other,one-m13,two)
                rt[one]=(13,0,13,0)
        blocks={(4,0):Block(4,0,f4,bytes(shared)),
                (13,0):Block(13,0,m13,bytes(other))}
        graph=Graph(blocks,rt)
        cursor=linked_external_graphics_block(graph,cursor_state,0x30)
        gen=linked_external_graphics_block(graph,genmenu_state,0x44)
        self.assertEqual(cursor["resource_root"],"0x02280020")
        self.assertEqual(gen["resource_root"],"0x02280120")
        self.assertEqual(cursor["typed_hops_verified"],6)
        self.assertFalse(cursor["identity_of_graphics_renderer_verified"])
        with self.assertRaises(Refused):
            linked_external_graphics_block(graph,cursor_state,0x10)
        missing=dict(rt)
        missing.pop(nodes_a[2])
        with self.assertRaises(Refused):
            linked_external_graphics_block(
                Graph(blocks,missing),cursor_state,0x30)
        backwards=bytearray(other)
        struct.pack_into("<I",backwards,nodes_a[2]-m13,nodes_a[1])
        with self.assertRaises(Refused):
            linked_external_graphics_block(
                Graph({**blocks,(13,0):Block(13,0,m13,bytes(backwards))},
                      rt),cursor_state,0x30)
        wrong=dict(rt)
        wrong[cursor_state+0x30]=(4,0,4,0)
        with self.assertRaises(Refused):
            linked_external_graphics_block(
                Graph(blocks,wrong),cursor_state,0x30)

    def test_direct_object_list_presence_is_not_a_renderer_proof(self):
        blocks,rt,actor=self.make_graph()
        default=actor_direct_object_list(Graph(blocks,rt),actor)
        self.assertFalse(default["direct_instance_object_list"])
        m4=blocks[(4,1)].base
        visual=int(actor["visual_data"],16)
        scene=bytearray(blocks[(4,1)].data)
        for offset in (0x0c,0x10):
            struct.pack_into("<I",scene,visual+offset-m4,m4+0xd0)
            rt[visual+offset]=(4,1,4,1)
        changed={**blocks,(4,1):Block(4,1,m4,bytes(scene))}
        self.assertTrue(actor_direct_object_list(
            Graph(changed,rt),actor)["direct_instance_object_list"])
        # Zero-valued fields must not carry typed relocation records.
        with self.assertRaises(Refused):
            actor_direct_object_list(Graph(blocks,rt),actor)
        different=bytearray(scene)
        struct.pack_into("<I",different,visual+0x10-m4,m4+0xd4)
        with self.assertRaises(Refused):
            actor_direct_object_list(Graph({
                **changed,(4,1):Block(4,1,m4,bytes(different))
            },rt),actor)
        no_type=dict(rt)
        no_type.pop(visual+0x0c)
        with self.assertRaises(Refused):
            actor_direct_object_list(Graph(changed,no_type),actor)

    def test_malformed_common_resources_refused(self):
        with self.assertRaises(Refused):
            decode_fix_rtb(bytes(50))
        with self.assertRaises((Refused,ValueError)):
            decode_fix_blocks(bytes.fromhex("79ccb56a"))


if __name__=="__main__":
    unittest.main()
