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
    return 0;
}
