#pragma once
// Rayman 2 original x86 keyboard polling: DirectInput device
// GetDeviceState(cbData=0x100, buffer), at retail RVA 0x966B3.
// Do not alter the input API globally. The exact hook is installed
// only in the single hash-approved executable and compiled only in
// the draft standalone VIDEO research build.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
namespace r2fix {
constexpr unsigned kVideoKeyboardBytes=256u;
constexpr unsigned kDikLeft=0xCBu;
constexpr unsigned kDikRight=0xCDu;
constexpr unsigned kDikUp=0xC8u;
constexpr unsigned kDikDown=0xD0u;
inline unsigned suppress_video_navigation_keys(
    bool authenticated_main_video,bool authenticated_pause_video,
    bool in_foreground,std::int32_t input_result,
    unsigned bytes,std::uint8_t* keyboard) noexcept {
    if(!(authenticated_main_video||authenticated_pause_video) ||
       !in_foreground || input_result!=0 ||
       bytes!=kVideoKeyboardBytes || !keyboard)return 0u;
    unsigned cleared=0;
    for(const unsigned code:{kDikUp,kDikDown,kDikLeft,kDikRight}) {
        if((keyboard[code]&0x80u)!=0u)++cleared;
        keyboard[code]=0u;
    }
    return cleared;
}
} // namespace r2fix
