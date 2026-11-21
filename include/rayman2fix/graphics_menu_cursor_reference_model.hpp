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
// Report only signed OFFSET from the verified name-table anchor;
// never expose raw process addresses. This does not make the source
// of a pointer value an actor/renderer. Return false when not nearby.
inline bool menu_cursor_relative_reference_offset(
    std::uintptr_t reference_site,std::uintptr_t name_anchor,
    int& offset) noexcept {
    if(!menu_cursor_reference_site_near_anchor(
           reference_site,name_anchor))return false;
    offset=static_cast<int>(
       static_cast<std::int64_t>(reference_site)-
       static_cast<std::int64_t>(name_anchor));
    return true;
}
// Test whether all 4 nearby reference sites fit inside one 256-byte
// region. This is merely a possible pointer table, not typed evidence.
constexpr bool menu_cursor_reference_offsets_clustered(
    const int (&offsets)[4],unsigned limit=256u) noexcept {
    int low=offsets[0],high=offsets[0];
    for(unsigned i=1;i<4;++i) {
        if(offsets[i]<low)low=offsets[i];
        if(offsets[i]>high)high=offsets[i];
    }
    return static_cast<unsigned>(high-low)<=limit;
}
} // namespace r2fix
