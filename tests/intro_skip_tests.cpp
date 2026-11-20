#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include "rayman2fix/scene_intro.hpp"
#include <unordered_map>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Explicitly check in both Debug and Release (assert is disabled by NDEBUG).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::abort(); \
    } \
} while (false)


int main() {
    CHECK(r2fix::is_second_intro("jail_20"));
    CHECK(r2fix::is_second_intro("Jail_20"));
    CHECK(!r2fix::is_second_intro("jail_10"));
    CHECK(!r2fix::is_second_intro("learn_10"));
    CHECK(!r2fix::is_second_intro("jail_201"));
    CHECK(!r2fix::is_second_intro(""));
    CHECK(!r2fix::is_second_intro(nullptr));

    r2fix::IntroSkipGuard guard;
    CHECK(!guard.update("menu_10", true));
    CHECK(!guard.update("jail_10", true));
    // If Enter is being held during the second intro load, do not skip.
    CHECK(!guard.update("jail_20", true));
    CHECK(!guard.update("jail_20", true));
    CHECK(!guard.update("jail_20", false));
    CHECK(guard.update("jail_20", true));
    // A single press can only request one level transition.
    CHECK(!guard.update("jail_20", true));
    CHECK(!guard.update("jail_20", false));
    CHECK(!guard.update("jail_20", true));
    // Re-entering a new game re-arms the feature.
    CHECK(!guard.update("learn_10", false));
    CHECK(!guard.update("jail_20", false));
    CHECK(!guard.update("jail_20", false));
    CHECK(guard.update("jail_20", true));

    // A fresh key press during native level initialization is retained.
    // Releasing the key or losing focus must not lose the queued request.
    r2fix::IntroSkipGuard queued;
    CHECK(!queued.update("jail_10", true, false));
    CHECK(!queued.update("jail_20", true, false));
    CHECK(!queued.update("jail_20", true, true));  // held from prior level
    CHECK(!queued.update("jail_20", false, false)); // first key release
    CHECK(!queued.update("jail_20", true, false));  // press while loading
    CHECK(queued.requested && queued.pending);
    CHECK(!queued.update("jail_20", false, false)); // focus lost
    CHECK(queued.update("jail_20", false, true));   // engine now Playing
    CHECK(!queued.pending);
    CHECK(!queued.update("jail_20", true, true));   // no duplicate
    CHECK(!queued.update("jail_20", false, true));
    CHECK(!queued.update("jail_20", true, true));   // second press ignored
    CHECK(!queued.update("learn_10", true, true));  // reset on exit
    CHECK(!queued.requested && !queued.pending);

    // If the level changes while waiting for readiness, cancel the request.
    r2fix::IntroSkipGuard cancelled;
    CHECK(!cancelled.update("jail_20", false, false));
    CHECK(!cancelled.update("jail_20", false, false));
    CHECK(!cancelled.update("jail_20", true, false));
    CHECK(cancelled.pending);
    CHECK(!cancelled.update("other_10", false, true));
    CHECK(!cancelled.pending && !cancelled.requested);

    // Learn_10 is a playable level; only a pending *intro* can be skipped.
    CHECK(r2fix::is_first_playable_level("Learn_10"));
    CHECK(r2fix::is_first_playable_level("learn_10"));
    CHECK(!r2fix::is_first_playable_level("jail_20"));
    CHECK(!r2fix::is_first_playable_level("learn_101"));
    CHECK(!r2fix::is_first_playable_level(nullptr));
    CHECK(r2fix::story_word_index(1091) == 34u);
    CHECK(r2fix::story_word_mask(1091) == 4u);
    CHECK(r2fix::story_word_index(1188) == 37u);
    CHECK(r2fix::story_word_mask(1188) == 8u);

    r2fix::LearnIntroGuard learn;
    CHECK(!learn.update("jail_20", false, true));
    CHECK(!learn.update("learn_10", true, true));
    CHECK(!learn.update("learn_10", true, true));
    CHECK(!learn.update("learn_10", true, false));
    CHECK(learn.update("learn_10", true, true));
    CHECK(!learn.update("learn_10", true, true));
    CHECK(!learn.update("learn_10", true, false));
    CHECK(learn.update("learn_10", true, true)); // retry after failed attempt
    CHECK(!learn.update("learn_10", true, true)); // holding won't repeat
    CHECK(!learn.update("learn_10", false, false));
    CHECK(!learn.update("learn_10", true, false));
    CHECK(!learn.update("learn_10", true, false));
    CHECK(learn.update("learn_10", true, true));
    CHECK(!learn.update("menu", false, false));

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
    CHECK(lookup() == values);
    mem.u32.erase(decl_ref);
    CHECK(lookup() == 0);             // missing second indirection
    mem.u32[decl_ref] = decl;
    mem.u32[infos + 42 * 16 + 4] = 7;
    CHECK(lookup() == 0);              // wrong variable type
    mem.u32[infos + 42 * 16 + 4] = 21;
    mem.u8[buffer + 0x120 + 4] = 36;
    CHECK(lookup() == 0);              // too few words for 1188
    mem.u8[buffer + 0x120 + 4] = 45;
    mem.u32.erase(actor + 0x0C);
    CHECK(lookup() == 0);              // inaccessible brain pointer


    {
    struct FakeActorMemory {
        std::unordered_map<std::uintptr_t,std::uint32_t> a;
        std::unordered_map<std::uintptr_t,std::uint8_t> b;
        bool u32(std::uintptr_t p,std::uint32_t& v) const {
            auto it=a.find(p); if(it==a.end()) return false;
            v=it->second;return true;
        }
        bool u8(std::uintptr_t p,std::uint8_t& v) const {
            auto it=b.find(p); if(it==b.end()) return false;
            v=it->second;return true;
        }
    } m;
    constexpr std::uintptr_t so=0x70000,ray=0x80000,actor=0x90000;
    constexpr std::uintptr_t standard=0xA0000,brain=0xB0000,mind=0xC0000;
    constexpr std::uintptr_t mem=0xD0000,ref=0xE0000,decl=0xF0000;
    constexpr std::uintptr_t infos=0x100000,buffer=0x110000,standin=0x130000;
    constexpr std::uintptr_t model=0x140000,script=0x150000,behaviours=0x160000;
    constexpr std::uintptr_t normal=0x170000;
    constexpr std::uintptr_t rayactor=0x1A0000,raystandard=0x1B0000;
    m.a[so]=2;m.a[so+4]=actor;
    m.a[actor+4]=standard;m.a[standard+0x24]=1u<<27;
    m.a[actor+0x0c]=brain;m.a[brain]=mind;
    m.a[mind+0x0c]=mem;m.a[mem]=ref;m.a[ref]=decl;
    m.a[decl+4]=infos;m.b[decl+0x0c]=7;m.a[mem+8]=buffer;
    for(unsigned i:{1u,2u,3u,4u})m.a[infos+i*16]=i*8;
    m.a[infos+1*16+4]=8;
    for(unsigned i:{2u,3u,4u})m.a[infos+i*16+4]=9;
    m.a[buffer+8]=0; // The ACTUAL intro's destination remains null.
    m.a[buffer+16]=so;m.a[buffer+24]=ray;m.a[buffer+32]=standin;
    m.a[standin]=2;
    m.a[ray+4]=rayactor;
    m.a[rayactor+4]=raystandard;
    m.a[raystandard+0x24]=(1u<<13)|(1u<<16);
    m.a[mind]=model;m.a[model]=script;
    m.a[script]=behaviours;m.a[script+4]=7;
    m.a[mind+4]=normal;m.a[normal+8]=behaviours+5*12;
    m.a[behaviours+12]=0x180000;
    m.a[behaviours+16]=0x190000;
    auto detect=[&](r2fix::IntroDirectorScanStats* stats=nullptr){
        return r2fix::recognize_intro_director(so,ray,
            [&](std::uintptr_t p,std::uint32_t& v){return m.u32(p,v);},
            [&](std::uintptr_t p,std::uint8_t& v){return m.u8(p,v);},stats);
    };
    r2fix::IntroDirectorScanStats stats{};
    auto found=detect(&stats);
    CHECK(found.valid());
    CHECK(found.superobject==so && found.intelligence==normal);
    CHECK(found.ending_behaviour==behaviours+12);
    CHECK(stats.identified_directors==1 && stats.null_waypoints==1);
    CHECK(stats.valid_behaviour_tables==1 && stats.in_intro_behaviour==1);
    CHECK(stats.rayman_suspended==1 && stats.current_behaviour_index==5);
    // Native intro can have other current normal behaviours at the
    // moment of the press, but only if Rayman is script-controlled.
    m.a[normal+8]=behaviours+2*12;
    r2fix::IntroDirectorScanStats other{};
    CHECK(detect(&other).valid());
    CHECK(other.other_behaviours==1 && other.current_behaviour_index==2);
    m.a[normal+8]=behaviours+12;
    r2fix::IntroDirectorScanStats ending{};
    CHECK(!detect(&ending).valid());
    CHECK(ending.already_ending==1);
    m.a[normal+8]=behaviours+5*12+4;
    r2fix::IntroDirectorScanStats misaligned{};
    CHECK(!detect(&misaligned).valid());
    CHECK(misaligned.outside_table==1);
    m.a[normal+8]=behaviours+5*12;
    m.a[raystandard+0x24]=1u<<13;
    r2fix::IntroDirectorScanStats unsuspended{};
    CHECK(!detect(&unsuspended).valid());
    CHECK(unsuspended.rayman_suspended==0);
    m.a[raystandard+0x24]=(1u<<13)|(1u<<16);
    m.a[script+4]=3;
    CHECK(!detect().valid()); // Need >=6 possible behaviours
    m.a[script+4]=7;
    m.a[buffer+24]=so;
    CHECK(!detect().valid()); // Wrong Rayman pointer
    m.a[buffer+24]=ray;
    m.a[standard+0x24]=0;
    CHECK(!detect().valid()); // Controller isn't armed
    m.a[standard+0x24]=1u<<27;
    CHECK(detect().valid());
    }

    return 0;
}
