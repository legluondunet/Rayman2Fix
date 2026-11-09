#pragma once

#include <cmath>

namespace r2fix {
// Rayman 2 GLI internally stores a camera angle in radians scaled by the
// classic 4:3 height/width ratio (0.75). Hor+ expands horizontal coverage
// without shrinking the original vertical field of view.
// Independent, deterministic math -- no game executable bytes or offsets.
inline float hor_plus_fov(float original_alpha_y, float output_aspect) {
    constexpr float original_aspect = 4.0f / 3.0f;
    if (!std::isfinite(original_alpha_y) || !std::isfinite(output_aspect) ||
        original_alpha_y <= 0.05f || original_alpha_y >= 2.25f ||
        output_aspect < original_aspect || output_aspect > 5.0f)
        return 0.0f;
    const float original_horizontal = original_alpha_y / 0.75f;
    const float new_horizontal = 2.0f * std::atan(
        std::tan(original_horizontal * 0.5f) *
        (output_aspect / original_aspect));
    return std::isfinite(new_horizontal) && new_horizontal > 0.0f &&
           new_horizontal < 3.0f ? new_horizontal : 0.0f;
}
}
