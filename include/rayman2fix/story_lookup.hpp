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
// Optional read-only audit. The refusal stage is stable and all numeric
// fields are metadata (not live addresses or the player's complete story).
struct ReplayHandoverAudit {
    const char* reason="not-scanned";
    unsigned actors=0, candidates=0, variables=0, bytes=0, offset=0, kind=0;
    unsigned current_kind=0, initial_kind=0, default_kind=0;
    unsigned current_count=0, initial_count=0, default_count=0;
    int current_bit=-1, initial_bit=-1, default_bit=-1;
    bool initial_present=false, default_present=false, separate_buffers=false;
};
template<class Read32,class Read8>
ReplayHandoverWord locate_replay_handover_word(std::uintptr_t list,
                                               Read32 rd32,Read8 rd8,
                                               ReplayHandoverAudit* audit=nullptr) {
    ReplayHandoverAudit local{};
    auto& d=audit ? *audit : local;
    d=ReplayHandoverAudit{};
    const auto fail=[&](const char* reason) {
        d.reason=reason;
        return ReplayHandoverWord{};
    };
    std::uint32_t n=0;
    if(!rd32(list,n)) return fail("fix-list-unreadable");
    d.actors=n;
    if(!n || n>32) return fail("fix-list-count");
    const auto get=[&](std::uint32_t a,unsigned off,std::uint32_t& v){
        return a>=0x10000u && a<=0xffffffffu-off &&
               rd32(std::uintptr_t(a)+off,v);
    };
    ReplayHandoverWord result{};
    for(unsigned i=0;i<n;++i) {
        std::uint32_t actor=0,stdgame=0,f=0,m=0,p=0;
        if(!rd32(list+4+4*i,actor) ||
           !get(actor,4,stdgame) ||
           !get(stdgame,0,f) || !get(stdgame,4,m) || !get(stdgame,8,p) ||
           f!=2 || m!=2 || p!=2)continue;
        if(++d.candidates!=1)return fail("duplicate-story-actor");
        std::uint32_t so=0,st=0,back=0,brain=0,mind=0,mem=0,ref=0,decl=0;
        std::uint32_t infos=0,bytes=0,off=0,kind=0,current=0,initial=0,defaults=0;
        std::uint8_t vars=0;
        if(!get(stdgame,12,so) || !get(so,0,st) || st!=2 ||
           !get(so,4,back) || back!=actor)
            return fail("story-actor-backlink");
        if(!get(actor,12,brain) || !get(brain,0,mind) ||
           !get(mind,12,mem) || !get(mem,0,ref) || !get(ref,0,decl))
            return fail("story-mind-declaration");
        if(!get(decl,4,infos) || !get(decl,8,bytes) ||
           decl>0xffffffffu-12u || !rd8(std::uintptr_t(decl)+12,vars))
            return fail("story-declaration-unreadable");
        d.bytes=bytes; d.variables=vars;
        if(bytes!=770 || vars!=68)return fail("story-declaration-shape");
        if(!get(infos,42*16,off) || !get(infos,42*16+4,kind))
            return fail("story-v42-unreadable");
        d.offset=off; d.kind=kind;
        if(off!=144 || kind!=21)return fail("story-v42-shape");
        const bool current_ok=get(mem,8,current) && current>=0x10000u;
        d.initial_present=get(mem,4,initial) && initial>=0x10000u;
        d.default_present=get(decl,0,defaults) && defaults>=0x10000u;
        d.separate_buffers=current_ok && d.initial_present && d.default_present &&
                           current!=initial && current!=defaults;
        const auto array_word=[&](std::uint32_t buf,std::uint32_t& word,
                                  unsigned& element_kind,unsigned& count_value,int& bit) {
            if(buf<0x10000u || buf>0xffffffffu-(144+8+45*4))return false;
            std::uint32_t element=0;
            std::uint8_t count=0;
            const bool type_ok=get(buf,144,element);
            const bool count_ok=rd8(std::uintptr_t(buf)+144+4,count);
            if(type_ok)element_kind=element;
            if(count_ok)count_value=count;
            if(!type_ok || !count_ok || element!=5 || count!=45 ||
               !get(buf,144+8+34*4,word))return false;
            bit=(word&4u)?1:0;
            return true;
        };
        std::uint32_t ci=0,ii=0,di=0;
        const bool c_ok=array_word(current,ci,d.current_kind,d.current_count,d.current_bit);
        const bool i_ok=array_word(initial,ii,d.initial_kind,d.initial_count,d.initial_bit);
        const bool d_ok=array_word(defaults,di,d.default_kind,d.default_count,d.default_bit);
        if(!d.separate_buffers)return fail("story-buffer-identity");
        if(!c_ok)return fail("story-current-array");
        if(!i_ok)return fail("story-initial-array");
        if(!d_ok)return fail("story-default-array");
        if((ii&4u) || (di&4u))return fail("story-reference-bit-set");
        result={std::uintptr_t(current)+144+8+34*4,ci};
    }
    if(!result.address)return fail("story-actor-id");
    d.reason="verified";
    return result;
}

} // namespace r2fix
