#pragma once
// Read-only discovery of the candidate PC input-action table.
// Reference: RayTools ACP_Ray2 (MIT), src/IPT/IPT.h and IPT.c.
// Retail CD compatibility is NOT assumed. Never use this probe to write.
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace r2fix {
constexpr std::uint32_t kIptCandidateRva = 0x109e60u;
constexpr std::uint32_t kIptTableHeaderOffset = 0x700u;
constexpr unsigned kIptObservedActions = 28;
struct IptTableHeader {
    std::uint32_t count = 0, table = 0;
};
struct IptActionReadback {
    std::uint32_t keyword_count = 0, keywords = 0, name = 0;
    std::int32_t state = 0;
    std::uint32_t analog_bits = 0;
    std::uint8_t active = 0;
    std::uint8_t padding[3]{}; // native 42E7A0 reads only byte +0x14
};
static_assert(sizeof(IptTableHeader) == 8);
static_assert(sizeof(IptActionReadback) == 24);
using IptReadbacks = std::array<IptActionReadback, kIptObservedActions>;
using IptNames = std::array<std::array<char, 64>, kIptObservedActions>;

constexpr bool ipt_header_plausible(IptTableHeader h) noexcept {
    return h.count >= kIptObservedActions && h.count <= 512 &&
        h.table >= 0x10000u && (h.table & 3u) == 0 &&
        h.table <= 0xffffffffu - h.count * sizeof(IptActionReadback);
}
constexpr bool ipt_entry_plausible(IptActionReadback a) noexcept {
    return (!a.name || (a.name >= 0x10000u && a.name <= 0xffffffbfu)) &&
        a.keyword_count <= 4096 && (!a.keyword_count || a.keywords >= 0x10000u) &&
        a.active <= 1u;
}
inline bool ipt_name_plausible(const std::array<char,64>& a) noexcept {
    for (unsigned i=0;i<a.size();++i) {
        const unsigned char c=static_cast<unsigned char>(a[i]);
        if (!c) return i>0;
        if (c<32 || c>126) return false;
    }
    return false;
}
inline bool ipt_name_contains(std::string_view name, std::string_view key) noexcept {
    const auto lower=[](char c){ return c>='A' && c<='Z' ? char(c+32) : c; };
    if(key.empty() || key.size()>name.size()) return false;
    for(std::size_t i=0;i+key.size()<=name.size();++i) {
        bool match=true;
        for(std::size_t k=0;k<key.size();++k)
            if(lower(name[i+k])!=lower(key[k])) { match=false; break; }
        if(match) return true;
    }
    return false;
}
// Expected names are hypotheses from the published reference's action
// enumeration, not proof of exact native CD semantics or writable ownership.
inline unsigned ipt_reference_anchors(const IptNames& names) noexcept {
    for(const auto& n:names) if(n[0] && !ipt_name_plausible(n)) return 0;
    const auto match=[&](unsigned i,std::string_view key) {
        return ipt_name_plausible(names[i]) && ipt_name_contains(names[i].data(),key);
    };
    return (match(0,"axex") ? 1u:0u) |
           (match(1,"axey") ? 2u:0u) |
           (match(10,"sauter") ? 4u:0u) |
           (match(11,"tirer") ? 8u:0u);
}
constexpr int ipt_state_sign(std::int32_t n) noexcept {
    return (n>0)-(n<0);
}
inline bool ipt_entry_changed(const IptActionReadback& a,
                              const IptActionReadback& b) noexcept {
    return ipt_state_sign(a.state)!=ipt_state_sign(b.state) ||
           a.analog_bits!=b.analog_bits || a.active!=b.active;
}
// Exact CD analog consumer reads signed-byte calibration and native
// integer conversion. Preserve its center and endpoints, not a fake key.
inline float ipt_calibrated_axis(float unit, int lo, int center, int hi) noexcept {
    if(lo < -128 || hi > 127 || lo >= center || hi <= center ||
       !(unit >= -1.f && unit <= 1.f)) return float(center);
    return float(center) + unit * float(unit < 0.f ? center-lo : hi-center);
}
} // namespace r2fix
