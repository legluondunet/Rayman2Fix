#pragma once
// Pure, testable validation of the MENU controller's V17 memory cell.
// No raw pointer writes or keyboard state are performed here.
#include <cstdint>
#include "rayman2fix/graphics_menu_controller_model.hpp"
namespace r2fix {
constexpr bool standalone_video_cursor_needs_reset(
    std::uint32_t selected) noexcept {
    // Only obsolete resolution rows 1..3; leave all other states alone.
    return selected>=1u && selected<=3u;
}
template<class R32,class R8>
bool locate_verified_video_cursor_cell(
    std::uintptr_t mind,R32 read32,R8 read8,
    std::uintptr_t& cell) noexcept {
    cell=0;
    if(!menu_safe_ptr(mind,16u))return false;
    std::uint32_t memory=0,ref=0,declaration=0,infos=0,values=0;
    std::uint8_t count=0;
    if(!read32(mind+12u,memory) || !menu_safe_ptr(memory,12u) ||
       !read32(memory,ref) || !menu_safe_ptr(ref,4u) ||
       !read32(ref,declaration) || !menu_safe_ptr(declaration,16u) ||
       !read32(declaration+4u,infos) ||
       !menu_safe_ptr(infos,90u*16u) ||
       !read8(declaration+12u,count) || count!=90u ||
       !read32(memory+8u,values) ||
       !menu_safe_ptr(values,0x4A6u))
        return false;
    const unsigned fields[3]{17u,19u,20u};
    const std::uint32_t offsets[3]{0x144u,0x14Cu,0x150u};
    for(unsigned i=0;i<3u;++i) {
        std::uint32_t offset=0,type=0;
        if(!read32(std::uintptr_t(infos)+fields[i]*16u,offset) ||
           !read32(std::uintptr_t(infos)+fields[i]*16u+4u,type) ||
           offset!=offsets[i] || type!=5u)return false;
    }
    cell=std::uintptr_t(values)+0x144u;
    if(!menu_safe_ptr(cell)) {cell=0;return false;}
    return true;
}
} // namespace r2fix
