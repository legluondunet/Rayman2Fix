#include "rayman2fix/scene_script_probe.hpp"
#include <cassert>
#include <cstdint>
#include <unordered_map>

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
