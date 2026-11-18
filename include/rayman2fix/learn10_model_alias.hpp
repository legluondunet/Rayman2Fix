#pragma once
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace r2fix {

// Read-only census of CURRENT world actors referencing one AI model.
// In the original Rayman 2 layout, actor -> brain (+0x0c) -> mind (+0)
// -> AIModel (+0). AIModel owns the script/behaviour arrays. A single
// actor in a world snapshot does NOT prove the model/script is private:
// actors can be spawned or exist in other resources.
struct Learn10ModelAliasCensus {
    bool complete = true;
    bool director_seen = false;
    bool director_matches_model = false;
    unsigned nodes_seen = 0;
    unsigned actors_seen = 0;
    unsigned matching_model_actors = 0;
    unsigned unreadable_actors = 0;
    // Informational only, never use this property to permit writes.
    bool apparently_unique_now() const {
        return complete && director_seen && director_matches_model &&
               matching_model_actors == 1u;
    }
    bool shared_now() const { return matching_model_actors > 1u; }
};

template<class Read32>
Learn10ModelAliasCensus survey_learn10_model_aliases(
    std::uintptr_t dynamic_world, std::uintptr_t inactive_world,
    std::uintptr_t director_superobject, std::uintptr_t expected_model,
    Read32 rd32, unsigned max_nodes = 4096u) {
    Learn10ModelAliasCensus result{};
    if (!dynamic_world || !inactive_world || !director_superobject ||
        !expected_model || dynamic_world == inactive_world ||
        max_nodes == 0u || max_nodes > 4096u) {
        result.complete = false;
        return result;
    }
    std::vector<std::pair<std::uintptr_t,std::uintptr_t>> pending;
    std::unordered_set<std::uintptr_t> seen;
    for (auto root : {dynamic_world, inactive_world}) {
        std::uint32_t child=0;
        if (!rd32(root+8u,child)) {
            result.complete = false;
            continue;
        }
        if (child) pending.emplace_back(child,root);
    }
    while (!pending.empty()) {
        const auto [so,parent]=pending.back();
        pending.pop_back();
        if (!so || !seen.insert(so).second || seen.size()>max_nodes) {
            result.complete=false;
            break;
        }
        ++result.nodes_seen;
        std::uint32_t kind=0,father=0,child=0,next=0;
        if (!rd32(so,kind) || !rd32(so+0x1cu,father) ||
            !rd32(so+8u,child) || !rd32(so+0x14u,next) ||
            father!=parent) {
            result.complete=false;
            break;
        }
        if (next) pending.emplace_back(next,parent);
        if (child) pending.emplace_back(child,so);
        if (so==director_superobject) result.director_seen=true;
        if (kind!=2u) continue; // HIE_C_Type_Actor only
        ++result.actors_seen;
        std::uint32_t actor=0,brain=0,mind=0,model=0;
        if (!rd32(so+4u,actor) || !actor ||
            !rd32(std::uintptr_t(actor)+0x0cu,brain) || !brain ||
            !rd32(std::uintptr_t(brain),mind) || !mind ||
            !rd32(std::uintptr_t(mind),model) || !model) {
            ++result.unreadable_actors;
            result.complete=false;
            continue;
        }
        if (std::uintptr_t(model)==expected_model)
            ++result.matching_model_actors;
        if (so==director_superobject)
            result.director_matches_model=std::uintptr_t(model)==expected_model;
    }
    if (!result.director_seen || !result.director_matches_model)
        result.complete=false;
    return result;
}

// No shared AIModel script mutation is authorised even if a census reports
// one current instance. Prove per-ACTOR shadow ownership and lifetime first.
constexpr bool kLearn10SharedScriptWritePermitted = false;

} // namespace r2fix
