#include "rayman2fix/standalone_video_cursor_policy.hpp"
#include <cstdlib>
#include <unordered_map>
using namespace r2fix;
void check(bool yes){if(!yes)std::abort();}
int main() {
    for(std::uint32_t n=0;n<=6u;++n)
        check(standalone_video_cursor_needs_reset(n)==(n>=1u && n<=3u));
    constexpr std::uintptr_t mind=0x10000u;
    constexpr std::uintptr_t mem=0x11000u,ref=0x12000u;
    constexpr std::uintptr_t decl=0x13000u,info=0x14000u;
    constexpr std::uintptr_t values=0x15000u;
    std::unordered_map<std::uintptr_t,std::uint32_t> data{
        {mind+12u,std::uint32_t(mem)}, {mem,std::uint32_t(ref)},
        {ref,std::uint32_t(decl)}, {decl+4u,std::uint32_t(info)},
        {mem+8u,std::uint32_t(values)},
        {info+17u*16u,0x144u},{info+17u*16u+4u,5u},
        {info+19u*16u,0x14Cu},{info+19u*16u+4u,5u},
        {info+20u*16u,0x150u},{info+20u*16u+4u,5u}
    };
    auto read32=[&](std::uintptr_t at,std::uint32_t& out) {
        const auto it=data.find(at);
        if(it==data.end())return false;
        out=it->second;return true;
    };
    auto read8=[&](std::uintptr_t at,std::uint8_t& out) {
        if(at!=decl+12u)return false;
        out=90u;return true;
    };
    std::uintptr_t cell=0;
    check(locate_verified_video_cursor_cell(mind,read32,read8,cell));
    check(cell==values+0x144u);
    for(std::uintptr_t at:{info+17u*16u,info+19u*16u,
                            info+20u*16u+4u}) {
        const auto original=data.at(at);
        data[at]=0xFFFFFFFFu;
        cell=0xFFFFFFFFu;
        check(!locate_verified_video_cursor_cell(mind,read32,read8,cell));
        check(cell==0u);
        data[at]=original;
    }
    check(!locate_verified_video_cursor_cell(0u,read32,read8,cell));
    check(cell==0u);
    std::uintptr_t lower=0,upper=0;
    check(locate_verified_video_selection_bounds(mind,read32,read8,
        cell,lower,upper));
    check(cell==values+0x144u && lower==values+0x14cu &&
          upper==values+0x150u);
    check(standalone_video_upper_bound_needs_reset(0u,3u));
    check(!standalone_video_upper_bound_needs_reset(0u,0u));
    check(!standalone_video_upper_bound_needs_reset(1u,3u));
    check(!standalone_video_upper_bound_needs_reset(0u,5u));
    check(!locate_verified_video_selection_bounds(0u,read32,read8,
        cell,lower,upper));
    check(cell==0u && lower==0u && upper==0u);
    return 0;
}
