#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace r2fix {

// Candidate ENGINE-LEVEL 4:3 placement for 3D actors marked as
// superimposed by original Rayman 2 scripts. This is intentionally
// independent of all draw callbacks and is NOT enabled at runtime.
// Do not apply it to a position already centered by a narrowed
// screen-world viewport: doing both would shrink X twice.
constexpr float kSuperimposedReferenceHeight = 480.0f;

// Native GLI_vGet3DVertexFromScreenPos accepts normalized screen X.
// The projection round trip may cancel a viewport-only change; apply
// the centered 4:3 mapping exactly ONCE to X before the inverse
// 2D-to-3D conversion, leaving Y and Z untouched.
inline bool center_normalized_superimposed_x(float original_x,
                                            float squeeze,
                                            float& result) {
    if(!std::isfinite(original_x) || std::fabs(original_x)>100.0f ||
       !std::isfinite(squeeze) || squeeze<=0.25f || squeeze>1.0f)
        return false;
    const float centered=0.5f+(original_x-0.5f)*squeeze;
    if(!std::isfinite(centered))return false;
    result=centered;
    return true;
}

// In the approved CD executable TextSuperImposed measures the native
// viewport width and height separately:
// VA 0x475BC6 loads the viewport WIDTH, VA 0x475BCC the HEIGHT;
// FMULs at 0x475BEF and 0x475BF7 divide by 640 and 480.
// Only the horizontal extent must be narrowed in panoramical mode.
// Applying the squeeze again to the vertical coefficient produced
// text squashed to 75% height at 16:9 (confirmed by screenshot pairs).
// Keep vertical coefficient invariant at any supported aspect ratio.
inline bool superimposed_text_extent_coefficients(
    float horizontal_squeeze, float& horizontal, float& vertical) {
    if(!std::isfinite(horizontal_squeeze) ||
       horizontal_squeeze<=0.25f || horizontal_squeeze>1.0f)
        return false;
    const float x=(1.0f/640.0f)*horizontal_squeeze;
    constexpr float y=1.0f/480.0f;
    if(!std::isfinite(x) || x<=0.0f) return false;
    horizontal=x;
    vertical=y;
    return true;
}

// Proc-style screen placement: the original actor procedure passes
// coordinates as thousandths of the screen, not as GLI 2D percent.
inline bool remap_superimposed_screen_x(std::int32_t x_1000,
                                       float squeeze,
                                       float& out_fraction) {
    if(!std::isfinite(squeeze) || squeeze<=0.25f || squeeze>1.0f)
        return false;
    const float fraction=static_cast<float>(x_1000)/1000.0f;
    const float centered=0.5f+(fraction-0.5f)*squeeze;
    if(!std::isfinite(centered))return false;
    out_fraction=centered;
    return true;
}

// The original superimposed-object script writes the same uniform
// extent into its X/Z matrix columns. Screen dimensions enter the
// size calculation separately from the position conversion.
// This historical helper is retained for regression comparisons; the
// widescreen runtime now restores the original Z/vertical matrix column.
inline bool superimposed_uniform_extent(float native_viewport_height,
                                        float squeeze,float& out_extent) {
    if(!std::isfinite(native_viewport_height) ||
       native_viewport_height<=0.0f || native_viewport_height>16384.0f ||
       !std::isfinite(squeeze) || squeeze<=0.25f || squeeze>1.0f)
        return false;
    const float scale=native_viewport_height/
        kSuperimposedReferenceHeight*squeeze;
    if(!std::isfinite(scale) || scale<=0.0f)return false;
    out_extent=scale;
    return true;
}

// VA 0x47122A and 0x4718CF multiply the approved retail viewport width
// by the same 1/640 coefficient. The original game stores that result
// at the matrix X and Z axes (VA 0x471230 and 0x4718D6 respectively).
// Previous widescreen code squeezed BOTH axes, visibly flattening the
// Ubisoft overlay/logo by 25% in 16:9. Keep X squeezed and restore Z.
inline bool superimposed_actor_extent_coefficients(
    float horizontal_squeeze, float& horizontal_coefficient,
    float& vertical_restoration_multiplier) {
    if(!std::isfinite(horizontal_squeeze) ||
       horizontal_squeeze<=0.25f || horizontal_squeeze>1.0f)
        return false;
    const float x=(1.0f/640.0f)*horizontal_squeeze;
    const float undo_vertical=1.0f/horizontal_squeeze;
    if(!std::isfinite(x) || x<=0.0f ||
       !std::isfinite(undo_vertical) || undo_vertical<=0.0f)
        return false;
    horizontal_coefficient=x;
    vertical_restoration_multiplier=undo_vertical;
    return true;
}

// Fixed-size x86 gateway for the TWO approved native actor-matrix store
// sites only. These both originally execute:
//   fst dword ptr [edi/esi+34h]
//   fstp dword ptr [edi/esi+54h]
// The replacement keeps the first (X), restores native size for the
// second (Z, vertical) with an x87 FMUL m32real, then jumps back.
// No register changes, no extra FPU stack elements and no world draw hooks.
// Return-location and factor addresses are x86 virtual addresses.
struct SuperimposedActorStoreGateway {
    std::array<std::uint8_t,17> bytes{};
};
inline bool make_superimposed_actor_store_gateway(
    bool use_edi,
    std::uint32_t multiplier_va,
    std::uint32_t gateway_va,
    std::uint32_t resume_va,
    SuperimposedActorStoreGateway& result) {
    if(!multiplier_va || !gateway_va || !resume_va)return false;
    const std::uint8_t base=use_edi ? 0x57 : 0x56;
    result.bytes={{
        0xD9,base,0x34, // FST [edi/esi + 0x34] (horizontal)
        0xD8,0x0D,0,0,0,0, // FMUL dword ptr [multiplier_va] (vertical)
        0xD9,static_cast<std::uint8_t>(base+0x08),0x54, // FSTP [edi/esi + 0x54]
        0xE9,0,0,0,0 // JMP return to the original native procedure
    }};
    std::memcpy(result.bytes.data()+5,&multiplier_va,4);
    const std::uint32_t relative=resume_va-
        (gateway_va+static_cast<std::uint32_t>(result.bytes.size()));
    std::memcpy(result.bytes.data()+13,&relative,4);
    return true;
}

} // namespace r2fix
