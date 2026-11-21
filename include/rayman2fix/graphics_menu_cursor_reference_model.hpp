#pragma once
// Pure read-only pointer-value classifier for the observed Menu.sna name
// block. These values identify possible references to scene strings;
// they DO NOT validate an actor, superobject, draw owner or pointer type.
#include <cstdint>
#include <cstddef>
namespace r2fix {
constexpr unsigned menu_cursor_reference_kind(std::uint32_t value,
                                              std::uintptr_t name_address) noexcept {
    // x86 image; the offsets are from the hash-verified Menu.sna
    // module-4 payload. Never use these candidates to dereference memory.
    if(name_address<0x10000u+444u ||
       name_address>0xFFFFFFFFu-2260u)return 0u;
    const auto name=static_cast<std::uint32_t>(name_address);
    if(value==name)return 1u;           // YAM_CurseurMenu_I1
    if(value==name+92u)return 2u;      // ARG_CurseurMenu
    if(value==name+2260u)return 3u;    // YAM_GenMenu_I1
    if(value==name-444u)return 4u;     // candidate module-4 base
    return 0u;
}
constexpr bool menu_cursor_reference_site_near_anchor(
    std::uintptr_t site,std::uintptr_t anchor) noexcept {
    return anchor>=65536u && site>=anchor-65536u &&
           site>=0x10000u && site<=anchor+65536u;
}
} // namespace r2fix
