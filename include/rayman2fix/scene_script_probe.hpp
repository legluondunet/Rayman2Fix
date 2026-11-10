#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace r2fix {

// A minimal, read-only shape reader for Rayman 2's 8-byte script nodes.
// Each script's first word points to its array of nodes. No game asset
// content, pointer values or executable code is emitted by this helper.
struct SceneScriptEvent {
    std::uint32_t node = 0;
    std::uint32_t param = 0; // used only for sanitized opcode formatting
    std::uint8_t indent = 0;
    std::uint8_t type = 0;
};
struct SceneScriptProbe {
    bool complete = false;
    bool malformed = false;
    bool truncated = false;
    unsigned visited = 0;
    unsigned selected = 0;
    std::array<SceneScriptEvent, 128> events{};
};
inline bool interesting_scene_opcode(std::uint8_t t) {
    switch(t) {
        case 0: case 1: case 2: case 3: case 4: case 5:
        case 9: case 11: case 12: case 17: case 23: case 28:
        case 39: return true;
        default: return false;
    }
}
// This intentionally never follows pointer-typed node parameters.
// It reads only the script's own bounds-checked node array.
template<class Read32, class Read8>
SceneScriptProbe observe_scene_script(std::uintptr_t script_slot,
                                     Read32 rd32, Read8 rd8) {
    SceneScriptProbe p{};
    std::uint32_t nodes=0;
    if (!script_slot || !rd32(script_slot,nodes) || nodes<0x10000)
        return p;
    constexpr unsigned max_nodes=1024;
    if (std::uintptr_t(nodes)>std::uintptr_t(0xFFFFFFFFu)-max_nodes*8u) {
        p.malformed=true;
        return p;
    }
    for (unsigned i=0;i<max_nodes;++i) {
        const auto at=std::uintptr_t(nodes)+std::uintptr_t(i)*8u;
        std::uint32_t param=0;
        std::uint8_t indent=0,type=0;
        if (!rd32(at,param) || !rd8(at+6,indent) || !rd8(at+7,type))
            return p;
        if (type>40 || indent>63 || (i==0 && indent!=1)) {
            p.malformed=true;
            return p;
        }
        p.visited=i+1;
        if (indent==0) {
            p.complete=(type==8);
            p.malformed=!p.complete;
            return p;
        }
        if (interesting_scene_opcode(type)) {
            if (p.selected<p.events.size())
                p.events[p.selected++]={i,param,indent,type};
            else
                p.truncated=true;
        }
    }
    p.truncated=true;
    return p;
}
} // namespace r2fix
