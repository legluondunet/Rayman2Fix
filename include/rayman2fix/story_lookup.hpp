#pragma once

#include <cstdint>

namespace r2fix {

// Rayman 2 keeps a count followed by DIRECT pointers to Fix actors.
// Those entries are not super objects: the actor's brain is at +0x0C,
// not at actor->superobject->actor+0x0C.
//
// Designer variable memory+0 points to a pointer to its declaration:
// memory -> declaration pointer -> declaration. The extra indirection is
// required by this game's AI variable layout (see r2live story_array).
//
// The story progress array is designer variable 42 (type 21). Its
// header has a byte word-count at +4 and u32 bitfield words at +8.
// All game-memory reads come from the provided checked functions.
template <typename Read32, typename Read8>
std::uintptr_t locate_story_words(std::uintptr_t fix_list,
                                  Read32 read32, Read8 read8) {
    std::uint32_t actors = 0;
    if (!read32(fix_list, actors) || actors == 0 || actors > 256)
        return 0;
    // The list is small in supported Rayman 2; cap traversal defensively.
    if (actors > 32) actors = 32;
    for (std::uint32_t i = 0; i < actors; ++i) {
        std::uint32_t actor = 0, brain = 0, mind = 0, memory = 0;
        std::uint32_t decl_ref = 0, decl = 0, infos = 0, buffer = 0, offset = 0, kind = 0;
        std::uint8_t variables = 0, words = 0;
        if (!read32(fix_list + 4u + 4u * i, actor) || !actor ||
            !read32(std::uintptr_t(actor) + 0x0Cu, brain) || !brain ||
            !read32(brain, mind) || !mind ||
            !read32(std::uintptr_t(mind) + 0x0Cu, memory) || !memory ||
            !read32(memory, decl_ref) || !decl_ref ||
            !read32(decl_ref, decl) || !decl ||
            !read32(std::uintptr_t(decl) + 4u, infos) || !infos ||
            !read8(std::uintptr_t(decl) + 0x0Cu, variables) || variables <= 42 ||
            !read32(std::uintptr_t(memory) + 8u, buffer) || !buffer ||
            !read32(std::uintptr_t(infos) + 42u * 16u, offset) ||
            !read32(std::uintptr_t(infos) + 42u * 16u + 4u, kind) ||
            kind != 21 || offset > 65536u)
            continue;
        const std::uintptr_t array = std::uintptr_t(buffer) + offset;
        // The two flags used by the scoped intro probe are bits 1091
        // and 1188, requiring at least 38 32-bit words.
        if (!read8(array + 4u, words) || words < 38 || words > 64)
            continue;
        const std::uintptr_t values = array + 8u;
        std::uint32_t probe = 0;
        if (!read32(values + 34u * 4u, probe) ||
            !read32(values + 37u * 4u, probe))
            continue;
        return values;
    }
    return 0;
}

// Stronger, write-authorization-independent lookup for the exact shared
// GOG progress actor. Original Fix data: IDs (2,2,2), 68 variables, 770
// bytes, V42 at 144, array kind 21, element kind 5, 45 words. Both initial
// and declaration copies must agree that bit 1091 starts clear. No data
// is written here; the runtime must separately prove a new-game route,
// game-thread ownership, writable storage and a successful atomic update.
struct ReplayHandoverWord {
    std::uintptr_t address=0;
    std::uint32_t value=0;
};
template<class Read32,class Read8>
ReplayHandoverWord locate_replay_handover_word(std::uintptr_t list,
                                               Read32 rd32,Read8 rd8) {
    std::uint32_t n=0;
    if(!rd32(list,n) || !n || n>32) return {};
    const auto get=[&](std::uint32_t a,unsigned off,std::uint32_t& v){
        return a>=0x10000u && a<=0xffffffffu-off &&
               rd32(std::uintptr_t(a)+off,v);
    };
    ReplayHandoverWord result{};
    unsigned candidates=0;
    for(unsigned i=0;i<n;++i) {
        std::uint32_t actor=0,stdgame=0,f=0,m=0,p=0;
        if(!rd32(list+4+4*i,actor) ||
           !get(actor,4,stdgame) ||
           !get(stdgame,0,f) || !get(stdgame,4,m) || !get(stdgame,8,p) ||
           f!=2 || m!=2 || p!=2)continue;
        if(++candidates!=1)return {};
        std::uint32_t so=0,st=0,back=0,brain=0,mind=0,mem=0,ref=0,decl=0;
        std::uint32_t infos=0,bytes=0,off=0,kind=0,current=0,initial=0,defaults=0;
        std::uint8_t vars=0;
        if(!get(stdgame,12,so) || !get(so,0,st) || st!=2 ||
           !get(so,4,back) || back!=actor ||
           !get(actor,12,brain) || !get(brain,0,mind) ||
           !get(mind,12,mem) || !get(mem,0,ref) || !get(ref,0,decl) ||
           !get(decl,4,infos) || !get(decl,8,bytes) || bytes!=770 ||
           decl>0xffffffffu-12u || !rd8(std::uintptr_t(decl)+12,vars) || vars!=68 ||
           !get(infos,42*16,off) || off!=144 ||
           !get(infos,42*16+4,kind) || kind!=21 ||
           !get(mem,8,current) || !get(mem,4,initial) || !get(decl,0,defaults) ||
           current==initial || current==defaults)return {};
        const auto array_word=[&](std::uint32_t buf,std::uint32_t& word) {
            std::uint32_t element=0;
            std::uint8_t count=0;
            return buf>=0x10000u && buf<=0xffffffffu-(144+8+45*4) &&
                   get(buf,144,element) && element==5 &&
                   rd8(std::uintptr_t(buf)+144+4,count) && count==45 &&
                   get(buf,144+8+34*4,word);
        };
        std::uint32_t ci=0,ii=0,di=0;
        if(!array_word(current,ci) || !array_word(initial,ii) ||
           !array_word(defaults,di) || (ii&4u) || (di&4u))return {};
        result={std::uintptr_t(current)+144+8+34*4,ci};
    }
    return result;
}

} // namespace r2fix
