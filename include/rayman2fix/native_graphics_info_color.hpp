#pragma once
// Experimental read-only VIDEO information color contract.
// Source: owner-observed native GLI font callback palette, 23 Nov 2026.
// A four-color array is copied to a local temporary buffer. No original
// game memory, V17, pointer table, butterfly, or brightness script is changed.
#include <cstdint>
namespace r2fix {
constexpr bool static_video_information_palette(
    const std::uint32_t (&source)[4],
    std::uint32_t (&destination)[4],
    bool verified_video_glyph,unsigned native_source_row,
    int native_focus) noexcept {
    if(!verified_video_glyph || native_source_row<1u ||
       native_source_row>4u || native_focus<1 || native_focus>4)
        return false;
    // The approved game's native font producer supplies two identical
    // vertex colors at each horizontal edge. Do not reinterpret unknown
    // material records as a 4×ARGB color array.
    if(source[0]!=source[1] || source[2]!=source[3])
        return false;
    if((source[0]&0x00FFFFFFu)!=0x00FFFFFFu)
        return false;
    const auto first_alpha=(source[0]>>24)&0xFFu;
    const auto second_alpha=(source[2]>>24)&0xFFu;
    if(first_alpha==0u || second_alpha!=first_alpha)return false;
    // A constant white shade, with the original element alpha, makes
    // text independent of the old resolution-choice highlight. Keep
    // fades, transitions and translucent disabled source-row alpha.
    const std::uint32_t uniform=source[0];
    for(auto& color:destination)color=uniform;
    return true;
}
} // namespace r2fix
