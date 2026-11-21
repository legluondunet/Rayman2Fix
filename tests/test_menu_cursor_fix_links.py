"""Synthetic, asset-free tests for Rayman 2 shared Fix -> Menu links."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
from research_menu_cursor_actor import Block,Graph,Refused
from research_menu_cursor_fix_links import (
    linked_family_state, linked_shared_ai_mind,
    actor_direct_object_list, cursor_normal_script_evidence,
    decode_fix_rtb, decode_fix_blocks
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

    def test_shared_fix_ai_mind_structure_and_fail_closed(self):
        f4,m13=0x001A0000,0x02280000
        shared=bytearray(0x400)
        ai=bytearray(0x700)
        rt={}
        state_a=f4+0x80
        state_b=f4+0x180
        for state,field,mi,ni,ri,li,ei,n in (
            (state_a,0x30,0x20,0x300,None,0x200,0x220,2),
            (state_b,0x44,0x120,0x360,0x3c0,0x250,0x270,23)):
            mind=m13+mi
            model=mind+0x18
            normal=m13+ni
            behavior_list=m13+li
            entry=m13+ei
            def put4(address,val):
                struct.pack_into("<I",ai,address-m13,val)
            def edge(source,target,source_mod=(13,0)):
                put4(source,target)
                rt[source]=(*source_mod,13,0)
            struct.pack_into("<I",shared,state+field-f4,mind)
            rt[state+field]=(4,0,13,0)
            edge(mind,model)
            edge(mind+4,normal)
            edge(mind+12,m13+0x510)
            put4(mind+0x14,6)
            edge(normal,model)
            if ri is not None:
                reflex=m13+ri
                edge(mind+8,reflex)
                edge(reflex,model+4)
            edge(model,behavior_list)
            edge(behavior_list,entry)
            put4(behavior_list+4,n)
        blocks={(4,0):Block(4,0,f4,bytes(shared)),
                (13,0):Block(13,0,m13,bytes(ai))}
        g=Graph(blocks,rt)
        cursor=linked_shared_ai_mind(g,state_a,0x30)
        gen=linked_shared_ai_mind(g,state_b,0x44)
        self.assertEqual(cursor["mind"],"0x02280020")
        self.assertEqual(cursor["normal_behaviors"],2)
        self.assertFalse(cursor["reflex_intelligence_present"])
        self.assertEqual(gen["normal_behaviors"],23)
        self.assertTrue(gen["reflex_intelligence_present"])
        self.assertTrue(gen["shared_ai_mind_verified"])
        self.assertFalse(gen["cursor_renderer_identified"])
        with self.assertRaises(Refused):
            linked_shared_ai_mind(g,state_a,0x10)
        missing=dict(rt)
        missing.pop(m13+0x300)
        with self.assertRaises(Refused):
            linked_shared_ai_mind(Graph(blocks,missing),state_a,0x30)
        forged=bytearray(ai)
        struct.pack_into("<I",forged,0x300,m13+0x120+0x18)
        with self.assertRaises(Refused):
            linked_shared_ai_mind(
                Graph({**blocks,(13,0):Block(13,0,m13,bytes(forged))},
                      rt),state_a,0x30)
        wrong=dict(rt)
        wrong[state_a+0x30]=(4,0,4,0)
        with self.assertRaises(Refused):
            linked_shared_ai_mind(Graph(blocks,wrong),state_a,0x30)
        excessive=bytearray(ai)
        struct.pack_into("<I",excessive,0x200+4,100000)
        with self.assertRaises(Refused):
            linked_shared_ai_mind(
                Graph({**blocks,(13,0):Block(13,0,m13,bytes(excessive))},
                      rt),state_a,0x30)

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

    def make_cursor_script_graph(self):
        m4,m13,fix=0x07000000,0x07900000,0x001A0000
        scene=bytearray(0x300)
        ai=bytearray(0x700)
        common=bytearray(0x700)
        rt={}
        brain=m4+0x30
        mind=m13+0x20
        model=mind+0x18
        normal=m13+0x100
        behaviors=m13+0x180
        entries=m13+0x1c0
        wrapper=m13+0x240
        nodes=m13+0x260
        state=fix+0x360
        family=fix+0x400
        actions=[fix+0x50,fix+0x150,fix+0x250]
        def link(array,base,source,target,typ):
            struct.pack_into("<I",array,source-base,target)
            rt[source]=typ
        link(scene,m4,brain,mind,(4,1,13,1))
        for source,target in (
            (mind,model),(mind+4,normal),(normal,model),
            (model,behaviors),(behaviors,entries),
            (normal+8,entries),(entries,wrapper),(wrapper,nodes)):
            link(ai,m13,source,target,(13,1,13,1))
        struct.pack_into("<I",ai,behaviors+4-m13,2)
        struct.pack_into("<I",ai,entries+8-m13,1)
        for idx in range(26):
            param=0
            typ=8 if idx==25 else 2
            indent=0 if idx==25 else 1
            if idx in (1,8,11,20):
                typ=23
                param=actions[{1:0,8:1,11:2,20:2}[idx]]
                rt[nodes+idx*8]=(13,1,4,0)
            struct.pack_into("<IHBB",ai,nodes+idx*8-m13,
                             param,0,indent,typ)
        for a in actions:
            own=a==actions[2]
            owner_state=state if own else fix+0x500
            owner_family=family if own else fix+0x580
            for delta in (0x3c,0x40,0x44):
                link(common,fix,a+delta,owner_state,(4,0,4,0))
            link(common,fix,a+0x50,owner_family,(4,0,4,0))
        blocks={(4,1):Block(4,1,m4,bytes(scene)),
                (13,1):Block(13,1,m13,bytes(ai)),
                (4,0):Block(4,0,fix,bytes(common))}
        actor={"brain":f"0x{brain:08X}"}
        family_info={"state":f"0x{state:08X}",
                     "family":f"0x{family:08X}"}
        return blocks,rt,actor,family_info,dict(
            brain=brain,mind=mind,normal=normal,entries=entries,
            nodes=nodes,actions=actions,fix=fix,m13=m13,m4=m4)

    def test_cursor_ai_action_links_and_fail_closed(self):
        blocks,rt,actor,family,ids=self.make_cursor_script_graph()
        graph=Graph(blocks,rt)
        result=cursor_normal_script_evidence(graph,actor,family)
        self.assertEqual(result["script_node_count"],26)
        self.assertEqual(result["action_ref_count"],4)
        self.assertEqual(result["distinct_action_ref_count"],3)
        self.assertEqual(result["own_state_action_refs"],2)
        self.assertFalse(result["visual_draw_owner_verified"])
        missing=dict(rt)
        missing.pop(ids["nodes"]+20*8)
        with self.assertRaises(Refused):
            cursor_normal_script_evidence(
                Graph(blocks,missing),actor,family)
        broken=dict(rt)
        broken.pop(ids["normal"]+8)
        with self.assertRaises(Refused):
            cursor_normal_script_evidence(
                Graph(blocks,broken),actor,family)
        common=bytearray(blocks[(4,0)].data)
        own=ids["actions"][2]
        struct.pack_into("<I",common,own+0x50-ids["fix"],ids["fix"]+0x580)
        with self.assertRaises(Refused):
            cursor_normal_script_evidence(
                Graph({**blocks,(4,0):Block(
                    4,0,ids["fix"],bytes(common))},rt),actor,family)
        ai=bytearray(blocks[(13,1)].data)
        ai[ids["nodes"]+25*8+7-ids["m13"]]=5
        with self.assertRaises(Refused):
            cursor_normal_script_evidence(
                Graph({**blocks,(13,1):Block(
                    13,1,ids["m13"],bytes(ai))},rt),actor,family)

    def test_malformed_common_resources_refused(self):
        with self.assertRaises(Refused):
            decode_fix_rtb(bytes(50))
        with self.assertRaises((Refused,ValueError)):
            decode_fix_blocks(bytes.fromhex("79ccb56a"))


if __name__=="__main__":
    unittest.main()
