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
                                       Read32 rd32, Read8 rd8) {
    IntroDirector answer{};
    if (!so || !rayman_so || so == rayman_so) return answer;
    std::uint32_t kind = 0, actor = 0, standard = 0, flags = 0;
    std::uint32_t brain = 0, mind = 0, memory = 0, decl_ref = 0;
    std::uint32_t decl = 0, infos = 0, buffer = 0;
    std::uint8_t vars = 0;
    if (!rd32(so, kind) || kind != 2 ||
        !rd32(so + 4, actor) || !actor ||
        !rd32(std::uintptr_t(actor) + 4, standard) || !standard ||
        !rd32(std::uintptr_t(standard) + 0x24, flags) ||
        (flags & (1u << 27)) == 0 ||
        !rd32(std::uintptr_t(actor) + 0x0c, brain) || !brain ||
        !rd32(brain, mind) || !mind ||
        !rd32(std::uintptr_t(mind) + 0x0c, memory) || !memory ||
        !rd32(memory, decl_ref) || !decl_ref ||
        !rd32(decl_ref, decl) || !decl ||
        !rd32(std::uintptr_t(decl) + 4, infos) || !infos ||
        !rd8(std::uintptr_t(decl) + 0x0c, vars) || vars < 7 ||
        !rd32(std::uintptr_t(memory) + 8, buffer) || !buffer)
        return answer;

    auto variable = [&](unsigned i, std::uint32_t type, std::uint32_t& value) {
        std::uint32_t offset = 0, kind = 0;
        if (!rd32(std::uintptr_t(infos) + i * 16, offset) ||
            !rd32(std::uintptr_t(infos) + i * 16 + 4, kind) ||
            kind != type || offset > 65536)
            return false;
        return rd32(std::uintptr_t(buffer) + offset, value);
    };
    std::uint32_t waypoint = 0, self = 0, subject = 0, standin = 0;
    if (!variable(1, 8, waypoint) || !waypoint ||
        !variable(2, 9, self) || self != so ||
        !variable(3, 9, subject) || subject != rayman_so ||
        !variable(4, 9, standin) || !standin || standin == rayman_so)
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
