#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace r2fix {

// Rayman 2's live actor tables on the SHA256-validated original executable.
// The scene director in Learn_10 has actor variables:
//   var1 = destination waypoint, var2 = self, var3 = Rayman, var4 = stand-in.
// Type 9 variables contain SUPER-OBJECT pointers, not actor pointers.
// All reads must be guarded by the passed in validated memory reader.
struct IntroDirectorScanStats {
    unsigned actor_objects = 0;
    unsigned armed_directors = 0;
    unsigned models_with_variables = 0;
    unsigned waypoint_kind = 0;
    unsigned waypoint_value = 0;
    unsigned self_kind = 0;
    unsigned self_value = 0;
    unsigned verified_self = 0;
    unsigned subject_kind = 0;
    unsigned subject_value = 0;
    unsigned verified_rayman = 0;
    unsigned standin_kind = 0;
    unsigned verified_standin = 0;
    unsigned identified_with_null_waypoint = 0;
};

struct IntroDirector {
    std::uintptr_t actor_superobject = 0;
    std::uintptr_t standin_superobject = 0;
    std::uintptr_t standin_transform = 0;
    float destination[3] = {};
    bool valid() const { return standin_transform != 0; }
};

template <typename Read32, typename Read8>
IntroDirector recognize_intro_director(std::uintptr_t so,
                                       std::uintptr_t rayman_so,
                                       Read32 rd32, Read8 rd8,
                                       IntroDirectorScanStats* stats = nullptr) {
    IntroDirector answer{};
    if (!so || !rayman_so || so == rayman_so) return answer;
    std::uint32_t kind = 0, actor = 0, standard = 0, flags = 0;
    std::uint32_t brain = 0, mind = 0, memory = 0, decl_ref = 0;
    std::uint32_t decl = 0, infos = 0, buffer = 0;
    std::uint8_t vars = 0;
    if (!rd32(so, kind) || kind != 2) return answer;
    if (stats) ++stats->actor_objects;
    if (!rd32(so + 4, actor) || !actor ||
        !rd32(std::uintptr_t(actor) + 4, standard) || !standard ||
        !rd32(std::uintptr_t(standard) + 0x24, flags) ||
        (flags & (1u << 27)) == 0) return answer;
    if (stats) ++stats->armed_directors;
    if (!rd32(std::uintptr_t(actor) + 0x0c, brain) || !brain ||
        !rd32(brain, mind) || !mind ||
        !rd32(std::uintptr_t(mind) + 0x0c, memory) || !memory ||
        !rd32(memory, decl_ref) || !decl_ref ||
        !rd32(decl_ref, decl) || !decl ||
        !rd32(std::uintptr_t(decl) + 4, infos) || !infos ||
        !rd8(std::uintptr_t(decl) + 0x0c, vars) || vars < 5 ||
        !rd32(std::uintptr_t(memory) + 8, buffer) || !buffer)
        return answer;
    if (stats) ++stats->models_with_variables;

    auto variable = [&](unsigned i, std::uint32_t type, std::uint32_t& value) {
        std::uint32_t offset = 0, kind = 0;
        if (!rd32(std::uintptr_t(infos) + i * 16, offset) ||
            !rd32(std::uintptr_t(infos) + i * 16 + 4, kind) ||
            kind != type || offset > 65536)
            return false;
        return rd32(std::uintptr_t(buffer) + offset, value);
    };
    // Diagnose independent designer variables even when the destination
    // waypoint is temporarily null. All final write preconditions remain
    // unchanged: a valid destination is REQUIRED.
    std::uint32_t waypoint = 0, self = 0, subject = 0, standin = 0;
    const bool waypoint_typed = variable(1, 8, waypoint);
    const bool self_typed = variable(2, 9, self);
    const bool subject_typed = variable(3, 9, subject);
    const bool standin_typed = variable(4, 9, standin);
    if (stats) {
        if (waypoint_typed) {
            ++stats->waypoint_kind;
            if (waypoint) ++stats->waypoint_value;
        }
        if (self_typed) {
            ++stats->self_kind;
            if (self) ++stats->self_value;
            if (self == so) ++stats->verified_self;
        }
        if (subject_typed) {
            ++stats->subject_kind;
            if (subject) ++stats->subject_value;
            if (subject == rayman_so) ++stats->verified_rayman;
        }
        if (standin_typed) {
            ++stats->standin_kind;
            if (standin && standin != rayman_so) ++stats->verified_standin;
        }
        if (waypoint_typed && !waypoint &&
            self_typed && self == so &&
            subject_typed && subject == rayman_so &&
            standin_typed && standin && standin != rayman_so)
            ++stats->identified_with_null_waypoint;
    }
    if (!waypoint_typed || !waypoint ||
        !self_typed || self != so ||
        !subject_typed || subject != rayman_so ||
        !standin_typed || !standin || standin == rayman_so)
        return answer;
    // A dynamic/attached waypoint needs a world-transform calculation:
    // refuse rather than teleport the stand-in to local coordinates.
    std::uint32_t attached = 0, standin_kind = 0, transform = 0;
    if (!rd32(std::uintptr_t(waypoint) + 0x10, attached) || attached ||
        !rd32(standin, standin_kind) || standin_kind != 2 ||
        !rd32(std::uintptr_t(standin) + 0x24, transform) || !transform)
        return answer;

    float dest[3], origin[3];
    for (unsigned i = 0; i < 3; ++i) {
        std::uint32_t x = 0, y = 0;
        if (!rd32(std::uintptr_t(waypoint) + i * 4, x) ||
            !rd32(std::uintptr_t(transform) + 4 + i * 4, y))
            return answer;
        std::memcpy(&dest[i], &x, sizeof(float));
        std::memcpy(&origin[i], &y, sizeof(float));
        if (!std::isfinite(dest[i]) || !std::isfinite(origin[i]) ||
            std::fabs(dest[i]) > 100000 || std::fabs(origin[i]) > 100000)
            return answer;
    }
    const double dx = double(dest[0]) - origin[0];
    const double dy = double(dest[1]) - origin[1];
    const double dz = double(dest[2]) - origin[2];
    const double distance = dx * dx + dy * dy + dz * dz;
    // Extremely remote coordinates or already-arrived stand-ins are
    // ambiguous. Do not modify actor state in these cases.
    if (distance < 0.04 || distance > 1000000.0)
        return answer;
    answer.actor_superobject = so;
    answer.standin_superobject = standin;
    answer.standin_transform = transform;
    for (unsigned i = 0; i < 3; ++i) answer.destination[i] = dest[i];
    return answer;
}
} // namespace r2fix
