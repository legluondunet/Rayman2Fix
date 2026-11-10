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
    // Real node-index/opcode landmarks observed in the Wine Learn_10
    // read-only trace. A complete matching fingerprint is an analysis
    // result, NOT authorisation to fast-forward or modify game state.
    {
        r2fix::SceneScriptProbe known{};
        known.complete = true;
        const auto add=[&](unsigned node,unsigned type,unsigned param) {
            assert(known.context_selected < known.context.size());
            known.context[known.context_selected++] = {
                node, param, 1, static_cast<std::uint8_t>(type)};
        };
        add(927,1,4);add(928,11,110);add(929,12,1);
        add(957,1,6);add(958,11,83);add(959,2,1);
        add(960,11,14);add(961,12,1);add(963,2,5);
        add(964,11,83);add(965,12,1);
        add(967,2,11);add(968,11,110);add(969,12,1);
        add(1022,4,84);add(1031,4,67);add(1034,4,68);
        add(1039,4,174);add(1040,12,14);add(1041,12,0);
        add(1044,4,174);add(1045,12,17);add(1046,12,0);
        add(933,4,174);add(934,12,30);add(935,12,0);
        add(938,4,69);add(939,28,0xDEADBEEF); // pointer ignored
        assert(r2fix::inspect_learn10_native_exit(known).matched());
        known.context[0].param=5;
        assert(!r2fix::inspect_learn10_native_exit(known).matched());
        known.context[0].param=4;
        known.context[known.context_selected-1].param=0xABCD1234;
        assert(r2fix::inspect_learn10_native_exit(known).matched());
        known.complete=false;
        assert(!r2fix::inspect_learn10_native_exit(known).matched());
        known.complete=true;
        known.malformed=true;
        assert(!r2fix::inspect_learn10_native_exit(known).matched());
    }
    return 0;
}
