#pragma once
#include <cstdint>

namespace r2fix {

// The original Rayman 2 engine's Learn_10 first-visit director is an
// actor with custom bit 28, var2 naming itself, var3 naming Rayman and
// var4 naming a distinct stand-in. var1 (waypoint) may be NULL throughout
// the intro; it is deliberately NOT a precondition for the native
// behaviour transition.
//
// AI_tdstMind +4 = normal intelligence
// AI_tdstMind +0 = model
// AI_tdstAIModel +0 = normal script (AI_tdstScriptAI)
// AI_tdstScriptAI +0 = behaviour array; +4 = behaviour count
// AI_tdstIntelligence +8 = current behaviour
// AI_tdstComport = 12 bytes on x86.
// See original symbols in raytools/ACP_Ray2, AI/AI.h and AI/AI_Scr.h.
//
// Behaviour 5 performs the cutscene. Behaviour 1 is the original
// controller's end script (clear bit 28, retire director). Switching to it
// through AI_fn_ucChangeComportIntell keeps the game's own script cleanup.
struct IntroDirectorScanStats {
    unsigned actor_objects = 0;
    unsigned armed_directors = 0;
    unsigned models_with_variables = 0;
    unsigned identified_directors = 0;
    unsigned null_waypoints = 0;
    unsigned valid_behaviour_tables = 0;
    unsigned in_intro_behaviour = 0;
    unsigned other_behaviours = 0;
};

struct IntroDirector {
    std::uintptr_t superobject = 0;
    std::uintptr_t intelligence = 0;
    std::uintptr_t ending_behaviour = 0;
    bool valid() const {
        return superobject != 0 && intelligence != 0 && ending_behaviour != 0;
    }
};

template <typename Read32, typename Read8>
IntroDirector recognize_intro_director(std::uintptr_t so,
                                       std::uintptr_t rayman_so,
                                       Read32 read32, Read8 read8,
                                       IntroDirectorScanStats* stats = nullptr) {
    IntroDirector result{};
    if (!so || !rayman_so || so == rayman_so) return result;
    std::uint32_t type=0, actor=0, standard=0, flags=0;
    std::uint32_t brain=0, mind=0, memory=0, declaration_ref=0;
    std::uint32_t declaration=0, infos=0, buffer=0;
    std::uint8_t variables=0;
    if (!read32(so,type) || type!=2) return result;
    if (stats) ++stats->actor_objects;
    if (!read32(so+4,actor) || !actor ||
        !read32(std::uintptr_t(actor)+4,standard) || !standard ||
        !read32(std::uintptr_t(standard)+0x24,flags) ||
        !(flags & (1u<<27))) return result;
    if (stats) ++stats->armed_directors;
    if (!read32(std::uintptr_t(actor)+0x0c,brain) || !brain ||
        !read32(brain,mind) || !mind ||
        !read32(std::uintptr_t(mind)+0x0c,memory) || !memory ||
        !read32(memory,declaration_ref) || !declaration_ref ||
        !read32(declaration_ref,declaration) || !declaration ||
        !read32(std::uintptr_t(declaration)+4,infos) || !infos ||
        !read8(std::uintptr_t(declaration)+0x0c,variables) || variables < 5 ||
        !read32(std::uintptr_t(memory)+8,buffer) || !buffer) return result;
    if (stats) ++stats->models_with_variables;

    auto variable = [&](unsigned index, unsigned kind, std::uint32_t& value) {
        std::uint32_t offset=0, type=0;
        return read32(std::uintptr_t(infos)+index*16,offset) &&
               read32(std::uintptr_t(infos)+index*16+4,type) &&
               type==kind && offset<=65536 &&
               read32(std::uintptr_t(buffer)+offset,value);
    };
    std::uint32_t waypoint=0,self=0,subject=0,standin=0;
    if (!variable(1,8,waypoint) || !variable(2,9,self) ||
        !variable(3,9,subject) || !variable(4,9,standin) ||
        self!=so || subject!=rayman_so || !standin ||
        standin==rayman_so || standin==so) return result;
    std::uint32_t standin_type=0;
    if (!read32(standin,standin_type) || standin_type!=2) return result;
    if (stats) {
        ++stats->identified_directors;
        if (!waypoint) ++stats->null_waypoints;
    }

    // Use *actual live engine model* instead of invented addresses or
    // per-level offsets. Require one of its six+ native behaviours to be
    // running and exactly the documented intro behaviour (index 5).
    std::uint32_t model=0, script=0, behaviours=0, count=0;
    std::uint32_t intelligence=0,current=0,rule=0,schedule=0;
    if (!read32(mind,model) || !model ||
        !read32(model,script) || !script ||
        !read32(script,behaviours) || !behaviours ||
        !read32(std::uintptr_t(script)+4,count) || count<6 || count>64 ||
        !read32(std::uintptr_t(mind)+4,intelligence) || !intelligence ||
        !read32(std::uintptr_t(intelligence)+8,current) || !current ||
        !read32(std::uintptr_t(behaviours)+12,rule) ||
        !read32(std::uintptr_t(behaviours)+16,schedule) ||
        (!rule && !schedule)) return result;
    if (stats) ++stats->valid_behaviour_tables;

    const std::uintptr_t intro=std::uintptr_t(behaviours)+5*12;
    if (current!=intro) {
        if (stats) ++stats->other_behaviours;
        return result;
    }
    if (stats) ++stats->in_intro_behaviour;
    result.superobject=so;
    result.intelligence=intelligence;
    result.ending_behaviour=std::uintptr_t(behaviours)+12;
    return result;
}
} // namespace r2fix
