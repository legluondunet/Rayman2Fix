#pragma once
// A post-wrapper, read-only probe of the original retail CD's 64-entry
// game sound table. This does NOT identify a native ADPCM record epoch.
#include <array>
#include <cstddef>
#include <cstdint>

namespace r2fix::esc_source_alias {

constexpr std::size_t kRows=64u;
constexpr std::size_t kStride=32u;
using RawTable=std::array<std::uint8_t,kRows*kStride>;

struct Summary {
    bool sampled=false;
    unsigned same_ref=0;
    unsigned transition_candidate=0;
    unsigned stamped_candidate=0;
    std::array<int,4> indices{{-1,-1,-1,-1}};
    // This comparison never establishes which native callbacks ran.
    bool authorize_audio=false;
};

constexpr std::uint32_t get32(const RawTable& src,std::size_t offset) noexcept {
    return std::uint32_t(src[offset]) |
        (std::uint32_t(src[offset+1])<<8u) |
        (std::uint32_t(src[offset+2])<<16u) |
        (std::uint32_t(src[offset+3])<<24u);
}

// Consumes a stable post-wrapper snapshot (two identical reads by caller),
// not a proof that entries were stable during original native dispatch.
// An original game source entry's +0 is passed through the type-1
// 0x4584A0 -> 0x4553A0 route to PauseTransitionSxd. A +0 pointer match
// does NOT establish that a particular source callback was dispatched.
// +0x1c is the game's depth stamp, NOT the physical ADPCM pause flag.
constexpr Summary summarize(const RawTable& table,bool two_reads_agree,
                            std::uint32_t observed_object,
                            std::uint32_t old_game_pause_depth) noexcept {
    Summary out{};
    if(!two_reads_agree || observed_object<0x10000u ||
       observed_object==0xffffffffu || old_game_pause_depth<1u ||
       old_game_pause_depth>16u)return out;
    out.sampled=true;
    for(std::size_t i=0;i<kRows;++i) {
        const std::size_t start=i*kStride;
        if(get32(table,start)!=observed_object)continue;
        if(out.same_ref<out.indices.size())
            out.indices[out.same_ref]=int(i);
        ++out.same_ref;
        if(get32(table,start+0x14u)!=0u &&
           get32(table,start+0x08u)==0u &&
           get32(table,start+0x10u)!=0u) {
            ++out.transition_candidate;
            if(get32(table,start+0x1cu)==old_game_pause_depth)
                ++out.stamped_candidate;
        }
    }
    return out;
}


struct ThemeRouteSummary {
    bool sampled=false;
    unsigned generic_candidate=0;
    unsigned theme_type11=0;
    unsigned theme_stamped=0;
    unsigned unknown_class=0;
    std::array<int,4> indices{{-1,-1,-1,-1}};
    // Post-wrapper table snapshots and type classification do not
    // establish which native callback actually fired, nor its voice epoch.
    bool authorize_audio=false;
};

// The caller obtains each descriptor type via the ORIGINAL generic
// sound-table route: record+4 -> descriptor -> descriptor+4.
// An entry with +0x10=0 is dispatched by 0x458070 through the native
// resource-type vtable; type 11 may delegate to the nested SXD record.
// "types" are already read-only, checked classifications (-1 unknown),
// not memory addresses or native physical voice identities.
constexpr ThemeRouteSummary summarize_theme_routes(
    const RawTable& table,
    const std::array<int,kRows>& types,
    bool coherent_table,
    std::uint32_t old_game_pause_depth) noexcept {
    ThemeRouteSummary out{};
    if(!coherent_table || old_game_pause_depth<1u ||
       old_game_pause_depth>16u)return out;
    out.sampled=true;
    for(std::size_t i=0;i<kRows;++i) {
        const std::size_t start=i*kStride;
        if(get32(table,start+0x14u)==0u ||
           get32(table,start+0x08u)!=0u ||
           get32(table,start+0x10u)!=0u)continue;
        ++out.generic_candidate;
        if(types[i]<0) {
            ++out.unknown_class;
            continue;
        }
        if(types[i]!=11)continue;
        if(out.theme_type11<out.indices.size())
            out.indices[out.theme_type11]=int(i);
        ++out.theme_type11;
        if(get32(table,start+0x1cu)==old_game_pause_depth)
            ++out.theme_stamped;
    }
    return out;
}

} // namespace r2fix::esc_source_alias
