#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include "rayman2fix/scene_intro.hpp"
#include <unordered_map>
#include <cstdint>
#include <cassert>

int main() {
    assert(r2fix::is_second_intro("jail_20"));
    assert(r2fix::is_second_intro("Jail_20"));
    assert(!r2fix::is_second_intro("jail_10"));
    assert(!r2fix::is_second_intro("learn_10"));
    assert(!r2fix::is_second_intro("jail_201"));
    assert(!r2fix::is_second_intro(""));
    assert(!r2fix::is_second_intro(nullptr));

    r2fix::IntroSkipGuard guard;
    assert(!guard.update("menu_10", true));
    assert(!guard.update("jail_10", true));
    // If Enter is being held during the second intro load, do not skip.
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", false));
    assert(guard.update("jail_20", true));
    // A single press can only request one level transition.
    assert(!guard.update("jail_20", true));
    assert(!guard.update("jail_20", false));
    assert(!guard.update("jail_20", true));
    // Re-entering a new game re-arms the feature.
    assert(!guard.update("learn_10", false));
    assert(!guard.update("jail_20", false));
    assert(!guard.update("jail_20", false));
    assert(guard.update("jail_20", true));

    // Learn_10 is a playable level; only a pending *intro* can be skipped.
    assert(r2fix::is_first_playable_level("Learn_10"));
    assert(r2fix::is_first_playable_level("learn_10"));
    assert(!r2fix::is_first_playable_level("jail_20"));
    assert(!r2fix::is_first_playable_level("learn_101"));
    assert(!r2fix::is_first_playable_level(nullptr));
    assert(r2fix::story_word_index(1091) == 34u);
    assert(r2fix::story_word_mask(1091) == 4u);
    assert(r2fix::story_word_index(1188) == 37u);
    assert(r2fix::story_word_mask(1188) == 8u);

    r2fix::LearnIntroGuard learn;
    assert(!learn.update("jail_20", false, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, false));
    assert(learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", true, false));
    assert(!learn.update("learn_10", true, true));
    assert(!learn.update("learn_10", false, false));
    assert(!learn.update("learn_10", true, false));
    assert(!learn.update("learn_10", true, false));
    assert(learn.update("learn_10", true, true));
    assert(!learn.update("menu", false, false));

    // The game's Fix-actor table holds DIRECT actor pointers, not
    // super-object pointers. Regression: an extra actor+4 dereference
    // previously made the entire Learn_10 feature unavailable.
    struct FakeMemory {
        std::unordered_map<std::uintptr_t, std::uint32_t> u32;
        std::unordered_map<std::uintptr_t, std::uint8_t> u8;
        bool rd32(std::uintptr_t p, std::uint32_t& v) const {
            const auto it = u32.find(p);
            if (it == u32.end()) return false;
            v = it->second;
            return true;
        }
        bool rd8(std::uintptr_t p, std::uint8_t& v) const {
            const auto it = u8.find(p);
            if (it == u8.end()) return false;
            v = it->second;
            return true;
        }
    } mem;
    constexpr std::uintptr_t list = 0x4B7300;
    constexpr std::uintptr_t actor = 0x120000;
    constexpr std::uintptr_t brain = 0x130000;
    constexpr std::uintptr_t mind = 0x140000;
    constexpr std::uintptr_t dsg = 0x150000;
    constexpr std::uintptr_t decl_ref = 0x158000;
    constexpr std::uintptr_t decl = 0x160000;
    constexpr std::uintptr_t infos = 0x170000;
    constexpr std::uintptr_t buffer = 0x180000;
    constexpr std::uintptr_t values = buffer + 0x120 + 8;
    mem.u32[list] = 3;                   // 3 Fix actor pointers
    mem.u32[list + 4] = 0x110000;        // unrelated actor
    mem.u32[list + 8] = 0;               // missing actor
    mem.u32[list + 12] = actor;          // correct DIRECT actor pointer
    mem.u32[actor + 0x0C] = brain;       // actor's brain, NOT actor->superobject
    mem.u32[brain] = mind;
    mem.u32[mind + 0x0C] = dsg;
    mem.u32[dsg] = decl_ref;
    mem.u32[decl_ref] = decl;
    mem.u32[decl + 4] = infos;
    mem.u8[decl + 0x0C] = 43;
    mem.u32[dsg + 8] = buffer;
    mem.u32[infos + 42 * 16] = 0x120;
    mem.u32[infos + 42 * 16 + 4] = 21;
    mem.u8[buffer + 0x120 + 4] = 45;
    mem.u32[values + 34 * 4] = 0;
    mem.u32[values + 37 * 4] = 0;
    auto lookup = [&] {
        return r2fix::locate_story_words(
            list,
            [&](std::uintptr_t p, std::uint32_t& v) { return mem.rd32(p, v); },
            [&](std::uintptr_t p, std::uint8_t& v) { return mem.rd8(p, v); });
    };
    assert(lookup() == values);
    mem.u32.erase(decl_ref);
    assert(lookup() == 0);             // missing second indirection
    mem.u32[decl_ref] = decl;
    mem.u32[infos + 42 * 16 + 4] = 7;
    assert(lookup() == 0);              // wrong variable type
    mem.u32[infos + 42 * 16 + 4] = 21;
    mem.u8[buffer + 0x120 + 4] = 36;
    assert(lookup() == 0);              // too few words for 1188
    mem.u8[buffer + 0x120 + 4] = 45;
    mem.u32.erase(actor + 0x0C);
    assert(lookup() == 0);              // inaccessible brain pointer


    {
    // A read-only candidate search only accepts a scene actor owning
    // its own custom bit 28, directing Rayman and a distinct stand-in
    // toward an unattached valid waypoint.
    struct Demo {
        std::unordered_map<std::uintptr_t,std::uint32_t> a;
        std::unordered_map<std::uintptr_t,std::uint8_t> b;
        bool read32(std::uintptr_t p,std::uint32_t& v) const {
            auto it=a.find(p); if(it==a.end()) return false;
            v=it->second; return true;
        }
        bool read8(std::uintptr_t p,std::uint8_t& v) const {
            auto it=b.find(p); if(it==b.end()) return false;
            v=it->second; return true;
        }
    } demo;
    constexpr std::uintptr_t scene=0x70000, ray=0x80000, actor=0x90000,
       standard=0xA0000, brain=0xB0000, mind=0xC0000, memory=0xD0000,
       ref=0xE0000, decl=0xF0000, infos=0x100000, buffer=0x110000,
       waypoint=0x120000, standin=0x130000, transform=0x140000;
    demo.a[scene]=2; demo.a[scene+4]=actor;
    demo.a[actor+4]=standard; demo.a[standard+0x24]=1u<<27;
    demo.a[actor+0x0c]=brain; demo.a[brain]=mind; demo.a[mind+0x0c]=memory;
    demo.a[memory]=ref; demo.a[ref]=decl;
    demo.a[decl+4]=infos; demo.b[decl+0x0c]=7;
    demo.a[memory+8]=buffer;
    for(unsigned i : {1u,2u,3u,4u}) demo.a[infos+i*16]=i*8;
    demo.a[infos+1*16+4]=8; demo.a[infos+2*16+4]=9;
    demo.a[infos+3*16+4]=9; demo.a[infos+4*16+4]=9;
    demo.a[buffer+8]=waypoint; demo.a[buffer+16]=scene;
    demo.a[buffer+24]=ray; demo.a[buffer+32]=standin;
    demo.a[waypoint+0x10]=0; demo.a[standin]=2;
    demo.a[standin+0x24]=transform;
    auto fbits=[](float x) {
        std::uint32_t v; std::memcpy(&v,&x,sizeof(v)); return v;
    };
    demo.a[waypoint]=fbits(30.0f);
    demo.a[waypoint+4]=fbits(295.0f);
    demo.a[waypoint+8]=fbits(-7.0f);
    demo.a[transform+4]=fbits(55.0f);
    demo.a[transform+8]=fbits(260.0f);
    demo.a[transform+12]=fbits(-7.0f);
    auto detect=[&] {
        return r2fix::recognize_intro_director(
            scene,ray,
            [&](std::uintptr_t p,std::uint32_t& v){return demo.read32(p,v);},
            [&](std::uintptr_t p,std::uint8_t& v){return demo.read8(p,v);});
    };
    const auto candidate=detect();
    assert(candidate.valid());
    assert(candidate.standin_transform==transform);
    assert(candidate.destination[0]==30.0f);
    demo.a[buffer+24]=scene;
    assert(!detect().valid());       // never skip scene with incorrect Rayman
    demo.a[buffer+24]=ray;
    demo.a[infos+4*16+4]=5;
    assert(!detect().valid());       // wrong variable kind
    demo.a[infos+4*16+4]=9;
    demo.a[waypoint+0x10]=scene;
    assert(!detect().valid());       // attached waypoint cannot be teleported to
    demo.a[waypoint+0x10]=0;
    demo.a[standard+0x24]=0;
    assert(!detect().valid());       // director isn't armed
    demo.a[standard+0x24]=1u<<27;
    assert(detect().valid());
    }

    return 0;
}
