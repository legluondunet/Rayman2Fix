#pragma once

#include <cstdint>

namespace r2fix {

// Rayman 2 keeps a count followed by DIRECT pointers to Fix actors.
// Those entries are not super objects: the actor's brain is at +0x0C,
// not at actor->superobject->actor+0x0C.
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
        std::uint32_t decl = 0, infos = 0, buffer = 0, offset = 0, kind = 0;
        std::uint8_t variables = 0, words = 0;
        if (!read32(fix_list + 4u + 4u * i, actor) || !actor ||
            !read32(std::uintptr_t(actor) + 0x0Cu, brain) || !brain ||
            !read32(brain, mind) || !mind ||
            !read32(std::uintptr_t(mind) + 0x0Cu, memory) || !memory ||
            !read32(memory, decl) || !decl ||
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
} // namespace r2fix
