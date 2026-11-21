#pragma once
// Pure verifier for the Rayman 2 CD Menu.sna module-4 name-table
// layout. This is NOT evidence of actor identity or its render path.
// Positions come from the maintainer's hash-verified scene inventory,
// not from a fixed live pointer or a redistributed game resource.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace r2fix {
constexpr std::size_t kMenuCursorActorAnchorWindow=2300u;
inline bool menu_cursor_name_present(const std::uint8_t* bytes,
                                     std::size_t n) noexcept {
    constexpr char kName[]="YAM_CurseurMenu_I1";
    return bytes && n>=sizeof(kName) &&
           std::memcmp(bytes,kName,sizeof(kName))==0;
}
inline bool verified_menu_cursor_name_anchor(
    const std::uint8_t* data,std::size_t size) noexcept {
    if(!data || size<kMenuCursorActorAnchorWindow)return false;
    constexpr char kActor[]="YAM_CurseurMenu_I1";
    constexpr char kCompanion[]="popolopoi_menu";
    constexpr char kCursor[]="ARG_CurseurMenu";
    constexpr char kMenuController[]="YAM_GenMenu_I1";
    return std::memcmp(data,kActor,sizeof(kActor))==0 &&
           std::memcmp(data+48u,kCompanion,sizeof(kCompanion))==0 &&
           std::memcmp(data+92u,kCursor,sizeof(kCursor))==0 &&
           std::memcmp(data+2260u,kMenuController,
                       sizeof(kMenuController))==0;
}
} // namespace r2fix
