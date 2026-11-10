#include "rayman2fix/scene_script_probe.hpp"
#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <utility>

struct Mock {
    std::unordered_map<std::uintptr_t,std::uint32_t> words;
    std::unordered_map<std::uintptr_t,std::uint8_t> bytes;
    bool read32(std::uintptr_t p,std::uint32_t& value) const {
        auto it=words.find(p);
        if(it==words.end())return false;
        value=it->second;
        return true;
    }
    bool read8(std::uintptr_t p,std::uint8_t& value) const {
        auto it=bytes.find(p);
        if(it==bytes.end())return false;
        value=it->second;
        return true;
    }
};
int main(){
    Mock m{};
    constexpr std::uintptr_t slot=0x20000,nodes=0x30000;
    m.words[slot]=nodes;
    auto set=[&](unsigned i,unsigned type,unsigned depth,std::uint32_t param){
        m.words[nodes+i*8]=param;
        m.bytes[nodes+i*8+6]=std::uint8_t(depth);
        m.bytes[nodes+i*8+7]=std::uint8_t(type);
    };
    set(0,0,1,6);      // keyword then
    set(1,1,2,78);     // condition 78
    set(2,4,2,139);    // procedure 139
    set(3,28,3,0xDEADBEEF); // pointer: must NOT be followed
    set(4,8,0,0);
    auto scan=[&]{
        return r2fix::observe_scene_script(
            slot,
            [&](std::uintptr_t p,std::uint32_t& v){return m.read32(p,v);},
            [&](std::uintptr_t p,std::uint8_t& v){return m.read8(p,v);});
    };
    auto p=scan();
    assert(p.complete && !p.malformed && !p.truncated);
    assert(p.visited==5 && p.selected==4);
    assert(p.events[2].type==4 && p.events[2].param==139);
    assert(p.events[3].type==28 && p.events[3].param==0xDEADBEEF);
    m.bytes[nodes+4*8+7]=3;
    p=scan();assert(!p.complete && p.malformed);
    m.bytes[nodes+4*8+7]=8;
    m.bytes[nodes+2*8+6]=64;
    p=scan();assert(!p.complete && p.malformed);
    m.bytes[nodes+2*8+6]=2;
    m.words.erase(nodes+2*8);
    p=scan();assert(!p.complete);
    m.words[nodes+2*8]=139;
    m.words[slot]=0; p=scan();
    assert(!p.complete && p.visited==0);
    // Native JustPressedBut(C45) operands are type 14 (Button).
    // They must be present in the sanitized event stream without
    // dereferencing them or interpreting large pointer-like values.
    {
        Mock button{};
        button.words[slot]=nodes;
        const auto set_button=[&](unsigned i,unsigned type,
                                  unsigned depth,unsigned value) {
            button.words[nodes+std::uintptr_t(i)*8]=value;
            button.bytes[nodes+std::uintptr_t(i)*8+6]=
                std::uint8_t(depth);
            button.bytes[nodes+std::uintptr_t(i)*8+7]=
                std::uint8_t(type);
        };
        set_button(0,0,1,0); // If
        set_button(1,1,2,45); // JustPressedBut
        set_button(2,14,3,42); // engine-defined button ID
        set_button(3,8,0,0); // end
        const auto result=r2fix::observe_scene_script(slot,
            [&](std::uintptr_t a,std::uint32_t& v){return button.read32(a,v);},
            [&](std::uintptr_t a,std::uint8_t& v){return button.read8(a,v);});
        assert(result.complete && !result.malformed);
        assert(result.selected==3);
        assert(result.events[2].type==14 && result.events[2].param==42u);
        set_button(2,14,3,0xFEEDC0DE); // never follows pointer-like data
        const auto opaque=r2fix::observe_scene_script(slot,
            [&](std::uintptr_t a,std::uint32_t& v){return button.read32(a,v);},
            [&](std::uintptr_t a,std::uint8_t& v){return button.read8(a,v);});
        assert(opaque.complete && opaque.events[2].param==0xFEEDC0DE);
    }
    // The large Learn_10 behaviour-3 rule 2 exceeded the old 1024-node
    // cap. Confirm that the targeted deeper probe is bounded and keeps
    // the LAST events and procedures in their original script order.
    {
        Mock big{};
        big.words[slot]=nodes;
        for (unsigned i=0;i<1600;++i) {
            big.words[nodes+std::uintptr_t(i)*8]=i&1 ? 174u : 139u;
            big.bytes[nodes+std::uintptr_t(i)*8+6]=1;
            big.bytes[nodes+std::uintptr_t(i)*8+7]=4;
        }
        big.words[nodes+1600u*8]=0;
        big.bytes[nodes+1600u*8+6]=0;
        big.bytes[nodes+1600u*8+7]=8;
        auto scan_big=[&](unsigned limit){
            return r2fix::observe_scene_script(
                slot,
                [&](std::uintptr_t a,std::uint32_t& v){return big.read32(a,v);},
                [&](std::uintptr_t a,std::uint8_t& v){return big.read8(a,v);},
                limit);
        };
        auto old=scan_big(1024);
        assert(!old.complete && old.visited==1024 && old.truncated);
        auto all=scan_big(4096);
        assert(all.complete && all.visited==1601 && all.truncated);
        assert(all.selected==128 && all.total_selected==1600);
        assert(all.tail_selected==96 && all.critical_selected==64);
        assert(all.critical_total==1600);
        const auto tail_begin=(all.total_selected-all.tail_selected)%all.tail.size();
        assert(all.tail[tail_begin].node==1600-96);
        const auto critical_begin=(all.critical_total-all.critical_selected)%all.critical.size();
        assert(all.critical[critical_begin].node==1600-64);
        assert(!scan_big(8193).complete);
    }
    // The optional exit-path window returns the original nesting order
    // for a specific subset of nodes, without leaking pointer operands.
    {
        Mock m2{};
        m2.words[slot]=nodes;
        for(unsigned i=0;i<240;++i){
            m2.words[nodes+std::uintptr_t(i)*8]=
                (i==120 ? 69u : 174u);
            m2.bytes[nodes+std::uintptr_t(i)*8+6]=std::uint8_t(1+(i%4));
            m2.bytes[nodes+std::uintptr_t(i)*8+7]=4;
        }
        m2.words[nodes+240u*8]=0;
        m2.bytes[nodes+240u*8+6]=0;
        m2.bytes[nodes+240u*8+7]=8;
        auto p2=r2fix::observe_scene_script(slot,
            [&](std::uintptr_t a,std::uint32_t& v){return m2.read32(a,v);},
            [&](std::uintptr_t a,std::uint8_t& v){return m2.read8(a,v);},
            1024u,100u,159u);
        assert(p2.complete && p2.context_selected==60u);
        assert(p2.context[0].node==100u && p2.context[59].node==159u);
        assert(p2.context[20].param==69u);
    }
    // Additional disjoint bounded windows must retain exactly the
    // selected node IDs and never include nodes outside their bounds.
    // The runtime uses the same observer to reconstruct the unread
    // centre of the actual retail director script.
    {
        Mock middle{};
        middle.words[slot]=nodes;
        for (unsigned i=0;i<1094;++i) {
            middle.words[nodes+std::uintptr_t(i)*8]=i;
            middle.bytes[nodes+std::uintptr_t(i)*8+6]=1;
            middle.bytes[nodes+std::uintptr_t(i)*8+7]=11;
        }
        middle.words[nodes+1093u*8]=0;
        middle.bytes[nodes+1093u*8+6]=0;
        middle.bytes[nodes+1093u*8+7]=8;
        const auto window=[&](unsigned first,unsigned last) {
            return r2fix::observe_scene_script(slot,
                [&](std::uintptr_t a,std::uint32_t& v){return middle.read32(a,v);},
                [&](std::uintptr_t a,std::uint8_t& v){return middle.read8(a,v);},
                8192u,first,last);
        };
        for (const auto& bounds : {std::pair<unsigned,unsigned>{134,383},
                                   {384,639},{640,869}}) {
            const auto p=window(bounds.first,bounds.second);
            assert(p.complete && !p.malformed);
            assert(p.context_selected==bounds.second-bounds.first+1);
            for (unsigned i=0;i<p.context_selected;++i) {
                assert(p.context[i].node==bounds.first+i);
                assert(p.context[i].param==bounds.first+i);
            }
        }
    }
    // Early original exit (v96==104) is not the six-stage pulse (110):
    // test the entire branch signature and reject missing cleanup,
    // wrong signal values and incorrect If/Then nesting.
    {
        r2fix::SceneScriptProbe early{};
        early.complete=true;
        const auto add=[&](unsigned node,unsigned depth,unsigned type,
                           unsigned param) {
            assert(early.context_selected<early.context.size());
            early.context[early.context_selected++]={
                node,param,std::uint8_t(depth),std::uint8_t(type)};
        };
        add(156,8,0,9); add(157,9,0,0); add(158,10,1,4);
        add(159,11,11,96);add(160,11,12,104);
        add(161,9,0,8);add(249,9,0,9);
        add(172,10,2,11);add(173,11,11,110);
        add(174,11,12,1);add(175,10,4,139);
        add(217,10,4,84);add(219,10,4,67);
        add(222,10,4,68);add(237,11,4,318);
        add(241,11,4,312);
        add(227,11,4,174);add(228,12,12,14);add(229,12,12,0);
        add(232,11,4,174);add(233,12,12,17);add(234,12,12,0);
        add(244,10,2,11);add(245,11,2,12);
        add(247,12,11,16);add(248,11,12,1);
        assert(r2fix::inspect_learn10_early_exit(early).matched());
        auto bad=early;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==160) bad.context[i].param=110;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==173) bad.context[i].param=109;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==161) bad.context[i].indent=10;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==232) bad.context[i].param=318;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==241) bad.context[i].indent=10;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;bad.complete=false;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
        bad=early;bad.malformed=true;
        assert(!r2fix::inspect_learn10_early_exit(bad).matched());
    }
    // Behaviour 3 rule 1 reads the real NOMOVIES cheat progress
    // bit 1188. Exact opcode/depth and field semantics must match.
    {
        r2fix::SceneScriptProbe script{};
        script.complete=true;
        const auto set=[&](unsigned i,unsigned depth,unsigned type,
                           unsigned param) {
            script.events[i]={i,param,std::uint8_t(depth),
                              std::uint8_t(type)};
        };
        set(0,1,0,1); set(1,2,3,248); set(2,3,2,12);
        set(3,4,23,0xBAADF00D); set(4,4,11,42);
        set(5,3,12,1188); set(6,1,0,8);
        script.selected=7;
        assert(r2fix::inspect_learn10_nomovies_gate(script).matched());
        auto bad=script;
        bad.events[5].param=1091;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.events[1].param=158;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.events[0].param=0;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.events[4].param=41;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.events[3].indent=3;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.events[3].node=7;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.malformed=true;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
        bad=script;bad.complete=false;
        assert(!r2fix::inspect_learn10_nomovies_gate(bad).matched());
    }
    // NOMOVIES=true enters the original top-level Else arm.
    // This includes a separate mode check and native behaviour
    // transition; it is NOT itself a user-visible skip.
    {
        r2fix::SceneScriptProbe alternative{};
        alternative.complete=true;
        const auto add=[&](unsigned node,unsigned depth,unsigned type,
                           unsigned param) {
            assert(alternative.context_selected<alternative.context.size());
            alternative.context[alternative.context_selected++]={
                node,param,std::uint8_t(depth),std::uint8_t(type)};
        };
        add(100,1,0,9); add(101,2,0,1); add(102,3,11,119);
        add(103,2,0,8); add(263,4,4,69);
        add(264,5,28,0xCAFEBABE); // pointer value not inspected
        assert(r2fix::inspect_learn10_nomovies_alternative(alternative).matched());
        auto bad=alternative;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==100) bad.context[i].indent=2;
        assert(!r2fix::inspect_learn10_nomovies_alternative(bad).matched());
        bad=alternative;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==102) bad.context[i].param=1188;
        assert(!r2fix::inspect_learn10_nomovies_alternative(bad).matched());
        bad=alternative;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==264) bad.context[i].type=11;
        assert(!r2fix::inspect_learn10_nomovies_alternative(bad).matched());
        bad=alternative;bad.complete=false;
        assert(!r2fix::inspect_learn10_nomovies_alternative(bad).matched());
    }
    // The known retail script has distinct If/Then/Else NESTING and a
    // model-relative behaviour-1 reference. Refuse lookalikes that have
    // the same opcodes but wrong nesting, target, or opcode values.
    {
        constexpr std::uintptr_t behaviours=0x44000;
        r2fix::SceneScriptProbe known{};
        known.complete=true;
        const auto add=[&](unsigned node,unsigned depth,unsigned type,
                           std::uint32_t param) {
            assert(known.context_selected<known.context.size());
            known.context[known.context_selected++]={
                node,param,static_cast<std::uint8_t>(depth),
                static_cast<std::uint8_t>(type)};
        };
        add(926,3,0,0);add(927,4,1,4);add(928,5,11,110);
        add(929,5,12,1);add(930,3,0,8);
        add(933,5,4,174);add(934,6,12,30);add(935,6,12,0);
        add(938,4,4,69);add(939,5,28,std::uint32_t(behaviours+12u));
        add(940,2,0,9);
        add(956,3,0,0);add(957,4,1,6);add(958,5,11,83);
        add(959,5,2,1);add(960,6,11,14);add(961,6,12,1);
        add(962,3,0,8);add(963,4,2,5);add(964,5,11,83);
        add(965,5,12,1);add(966,3,0,9);
        add(967,4,2,11);add(968,5,11,110);add(969,5,12,1);
        add(970,4,4,139);
        add(1022,4,4,84);add(1028,5,4,312);
        add(1031,4,4,67);add(1034,4,4,68);
        add(1039,5,4,174);add(1040,6,12,14);add(1041,6,12,0);
        add(1044,5,4,174);add(1045,6,12,17);add(1046,6,12,0);
        add(1049,5,4,318);
        assert(r2fix::inspect_learn10_native_exit(known,behaviours,6).matched());
        auto bad=known;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==966) bad.context[i].indent=2;
        assert(!r2fix::inspect_learn10_native_exit(bad,behaviours,6).matched());
        bad=known;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==939) bad.context[i].param=
                std::uint32_t(behaviours+24u); // wrong behaviour
        assert(!r2fix::inspect_learn10_native_exit(bad,behaviours,6).matched());
        assert(!r2fix::inspect_learn10_native_exit(known,behaviours+12u,6).matched());
        bad=known;
        for(unsigned i=0;i<bad.context_selected;++i)
            if(bad.context[i].node==927) bad.context[i].param=5;
        assert(!r2fix::inspect_learn10_native_exit(bad,behaviours,6).matched());
        bad=known;
        bad.complete=false;
        assert(!r2fix::inspect_learn10_native_exit(bad,behaviours,6).matched());
        bad=known;
        bad.malformed=true;
        assert(!r2fix::inspect_learn10_native_exit(bad,behaviours,6).matched());
        assert(!r2fix::inspect_learn10_native_exit(known,0,6).matched());
    }
    // The phase locator must match an actual nested condition with
    // contiguous operands. A coincidental literal 110 or unrelated V96
    // must not be misidentified as the phase comparison.
    {
        Mock phase{};
        phase.words[slot]=nodes;
        auto set_phase=[&](unsigned i,unsigned type,unsigned depth,
                           std::uint32_t value) {
            phase.words[nodes+std::uintptr_t(i)*8]=value;
            phase.bytes[nodes+std::uintptr_t(i)*8+6]=std::uint8_t(depth);
            phase.bytes[nodes+std::uintptr_t(i)*8+7]=std::uint8_t(type);
        };
        set_phase(0,0,1,0); // if
        set_phase(1,1,2,4); // Equal
        set_phase(2,11,3,96); // var96
        set_phase(3,12,3,110); // literal 110
        set_phase(4,0,1,8); // then
        set_phase(5,8,0,0); // end
        const auto scan_phase=[&] {
            return r2fix::observe_scene_script(slot,
                [&](std::uintptr_t a,std::uint32_t& v){return phase.read32(a,v);},
                [&](std::uintptr_t a,std::uint8_t& v){return phase.read8(a,v);});
        };
        auto p=scan_phase();
        assert(p.complete && !p.malformed && p.phase110_predicates==1);
        assert(p.phase110_first_node==1 && p.phase110_first_condition==4);
        set_phase(3,12,3,111);
        assert(scan_phase().phase110_predicates==0);
        set_phase(3,12,3,110);
        set_phase(2,11,3,95);
        assert(scan_phase().phase110_predicates==0);
        set_phase(2,11,3,96);
        set_phase(2,11,2,96); // wrong nested operand
        assert(scan_phase().phase110_predicates==0);
        set_phase(2,11,3,96);
        set_phase(1,1,2,49); // unrelated condition
        assert(scan_phase().phase110_predicates==0);
        set_phase(1,1,2,6); // lesser-than comparison
        assert(scan_phase().phase110_predicates==1);
        // An unrelated node between the variable and literal breaks
        // the exact expression fingerprint.
        set_phase(3,2,3,11);
        set_phase(4,12,3,110);
        assert(scan_phase().phase110_predicates==0);
    }
    return 0;
}
