#include "rayman2fix/intro_skip.hpp"
#include "rayman2fix/story_lookup.hpp"
#include "rayman2fix/scene_intro.hpp"
#include <unordered_map>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Explicitly check in both Debug and Release (assert is disabled by NDEBUG).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::abort(); \
    } \
} while (false)


int main() {
    CHECK(r2fix::intro_direct_transition_allowed(true,false));
    CHECK(!r2fix::intro_direct_transition_allowed(true,true));
    CHECK(r2fix::intro_direct_transition_allowed(false,false));
    CHECK(r2fix::intro_direct_transition_allowed(false,true));

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

    // A replay request can reach Playing while the original controller
    // remains in B2. Retain the press, throttle preflight, and dispatch
    // once only after the original B1 authorization succeeds.
    r2fix::IntroSkipGuard replay;
    r2fix::ReplayOwnerWait wait;
    CHECK(!replay.update("jail_20",false,false));
    CHECK(!replay.update("jail_20",false,false));
    CHECK(!replay.update("jail_20",true,false));
    CHECK(replay.update("jail_20",false,true));
    CHECK(wait.defer(2,553));
    replay.defer_pre_dispatch();
    CHECK(replay.pending && replay.requested);
    CHECK(!wait.due(602));
    CHECK(!replay.update("jail_20",false,wait.due(602)));
    CHECK(wait.due(603) && !wait.expired(603));
    CHECK(replay.update("jail_20",false,true));
    CHECK(wait.defer(2,603));
    replay.defer_pre_dispatch();
    CHECK(replay.update("jail_20",false,wait.due(653)));
    wait.clear(); // B1 and all other original guards passed, native dispatch
    CHECK(!replay.update("jail_20",true,true));
    CHECK(!replay.update("jail_20",false,true));
    CHECK(!replay.update("jail_20",true,true));
    CHECK(!wait.defer(-1,700) && !wait.defer(0,700) && !wait.defer(1,700));
    CHECK(wait.defer(2,1000));
    CHECK(!wait.expired(2999) && wait.expired(3000));
    CHECK(!wait.defer(2,3000));
    wait.clear();
    CHECK(wait.defer(2,0xfffffff0u));
    CHECK(!wait.due(0x10u) && wait.due(0x22u));
    CHECK(wait.expired(0x7c0u));
    wait.clear();
    // A refusal before dispatch must never consume the entire scene.
    // Releasing and pressing again can attempt the full guarded preflight.
    replay.refuse_pre_dispatch();
    CHECK(!replay.update("jail_20",true,true));
    CHECK(!replay.update("jail_20",false,true));
    CHECK(replay.update("jail_20",true,true));
    replay.defer_pre_dispatch();
    CHECK(!replay.update("learn_10",false,true));
    CHECK(!replay.pending && !replay.requested);

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


    // The replay route is earned by a real handover, not by an already
    // playable loaded save or a stale story bit on entry.
    r2fix::ReplayIntroRoute route;
    route.observe("menu"); route.observe("jail_10"); route.observe("jail_20");
    CHECK(!route.ready());
    route.note_handover();
    route.observe("menu"); route.observe("jail_20");
    CHECK(!route.ready());
    route.observe("MENU"); route.observe("JAIL_10");
    route.observe("other_10"); route.observe("jail_20");
    CHECK(!route.ready());
    route.observe("menu"); route.observe("jail_10");
    route.observe("jail_10"); route.observe("jail_20"); route.observe("jail_20");
    CHECK(route.ready());
    route.consume();
    CHECK(!route.ready());
    route.observe("menu"); route.observe("jail_10"); route.observe("jail_20");
    CHECK(!route.ready());
    route.note_handover(); route.observe("menu");
    route.observe("jail_10"); route.observe("jail_20");
    CHECK(route.ready());
    route.observe(nullptr);
    CHECK(!route.ready());
    // Pause B13/R0 can request Jail_10 directly, without MENU.
    r2fix::ReplayIntroRoute pause_route;
    pause_route.observe("Learn_10",true);
    pause_route.observe("jail_10"); pause_route.observe("jail_20");
    CHECK(!pause_route.ready());
    pause_route.note_handover();
    pause_route.observe("Learn_10",true);
    pause_route.observe("Learn_10",false,true);
    pause_route.observe(nullptr,false,true);
    pause_route.observe("jail_10");
    pause_route.observe("",false,false,true);
    pause_route.observe("jail_20");
    CHECK(pause_route.ready());
    pause_route.consume();
    pause_route.note_handover();
    pause_route.observe("Learn_10",true);
    pause_route.observe("Learn_10",false); // cancelled/resumed gameplay
    pause_route.observe("jail_10"); pause_route.observe("jail_20");
    CHECK(!pause_route.ready());
    pause_route.observe("Learn_10",true);
    pause_route.observe("mapmonde"); // alternative native branch
    pause_route.observe("jail_10"); pause_route.observe("jail_20");
    CHECK(!pause_route.ready());
    pause_route.observe("Learn_10",true);
    pause_route.observe(nullptr,false,false,true); // wrong queued target
    pause_route.observe("jail_10"); pause_route.observe("jail_20");
    CHECK(!pause_route.ready());
    pause_route.observe("Learn_10",true);
    pause_route.observe("jail_10"); pause_route.observe("jail_20");
    CHECK(pause_route.ready());
    for(unsigned bit=0;bit<32;++bit) {
        const auto word=std::uint32_t(1u)<<bit;
        CHECK(r2fix::replay_handover_reset_word(word)==(bit==2?0u:word));
    }
    CHECK(r2fix::replay_handover_reset_word(0xffffffffu)==0xfffffffbu);

    // Reuse the synthetic Fix-actor memory with the exact GOG layout.
    {
        FakeMemory exact;
        constexpr std::uintptr_t stdg=0x190000,so2=0x1a0000;
        constexpr std::uintptr_t initial=0x1b0000,defaults=0x1c0000;
        exact.u32[list]=1; exact.u32[list+4]=actor;
        exact.u32[actor+4]=stdg; exact.u32[actor+12]=brain;
        exact.u32[stdg]=2; exact.u32[stdg+4]=2; exact.u32[stdg+8]=2;
        exact.u32[stdg+12]=so2; exact.u32[so2]=2; exact.u32[so2+4]=actor;
        exact.u32[brain]=mind; exact.u32[mind+12]=dsg;
        exact.u32[dsg]=decl_ref; exact.u32[decl_ref]=decl;
        exact.u32[dsg+4]=initial; exact.u32[dsg+8]=buffer;
        exact.u32[decl]=defaults; exact.u32[decl+4]=infos;
        exact.u32[decl+8]=770; exact.u8[decl+12]=68;
        exact.u32[infos+42*16]=144; exact.u32[infos+42*16+4]=21;
        constexpr auto offset=144+8+34*4;
        for(auto ptr : {buffer,initial,defaults}) {
            exact.u32[ptr+144]=5; exact.u8[ptr+144+4]=45;
            exact.u32[ptr+offset]=0;
        }
        exact.u32[buffer+offset]=0x80000007u;
        r2fix::ReplayHandoverAudit audit{};
        auto get=[&] {
            return r2fix::locate_replay_handover_word(list,
                [&](std::uintptr_t p,std::uint32_t& v){return exact.rd32(p,v);},
                [&](std::uintptr_t p,std::uint8_t& v){return exact.rd8(p,v);},&audit);
        };
        CHECK(get().address==buffer+offset);
        CHECK(get().value==0x80000007u);
        CHECK(std::strcmp(audit.reason,"verified")==0);
        CHECK(audit.current_bit==1 && audit.initial_bit==0 && audit.default_bit==0);
        CHECK(audit.separate_buffers && audit.current_count==45);
        CHECK(r2fix::replay_handover_reset_word(get().value)==0x80000003u);
        exact.u32[initial+offset]=4; CHECK(!get().address);
        CHECK(std::strcmp(audit.reason,"story-reference-bit-set")==0);
        CHECK(audit.current_bit==1 && audit.initial_bit==1 && audit.default_bit==0);
        exact.u32[initial+offset]=0;
        exact.u32[defaults+offset]=4; CHECK(!get().address);
        exact.u32[defaults+offset]=0;
        exact.u32[dsg+4]=buffer; CHECK(!get().address);
        CHECK(std::strcmp(audit.reason,"story-buffer-identity")==0);
        exact.u32[dsg+4]=initial;
        exact.u8[buffer+148]=44; CHECK(!get().address);
        CHECK(std::strcmp(audit.reason,"story-current-array")==0);
        CHECK(audit.current_count==44 && audit.initial_bit==0 && audit.default_bit==0);
        exact.u8[buffer+148]=45;
        exact.u32[stdg+8]=3; CHECK(!get().address);
        exact.u32[stdg+8]=2;
        exact.u32[so2+4]=0; CHECK(!get().address);
        exact.u32[so2+4]=actor;
        exact.u32[list]=2; exact.u32[list+8]=actor; CHECK(!get().address);
        exact.u32[list]=1;
        CHECK(get().address==buffer+offset);
    }

    return 0;
}
