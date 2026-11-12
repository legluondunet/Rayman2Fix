#pragma once

#include <cmath>
#include <cstdint>

namespace r2fix {

// Candidate ENGINE-LEVEL 4:3 placement for 3D actors marked as
// superimposed by original Rayman 2 scripts. This is intentionally
// independent of all draw callbacks and is NOT enabled at runtime.
// Do not apply it to a position already centered by a narrowed
// screen-world viewport: doing both would shrink X twice.
constexpr float kSuperimposedReferenceHeight = 480.0f;

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

} // namespace r2fix
