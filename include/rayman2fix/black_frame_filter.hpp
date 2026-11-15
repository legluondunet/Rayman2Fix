#pragma once

#include <cmath>

namespace r2fix {

// Only applies to the independently verified GLI_vDisplayFrame callsite
// (native screen-space 2D frame quads, not glyphs, images, or world sprites).
// Classification is on ORIGINAL 0..100 percent coordinates, before the
// existing centered 4:3 HUD transform. All boundaries are conservative.
// This is a VISUAL opt-in filter: an opaque material color is not inferred
// from a rectangle's position. Never use this to erase arbitrary quads.
enum class BlackFrameKind {
    Preserve,
    DialogueBackdrop,
    CinematicLetterbox
};

inline BlackFrameKind black_frame_kind(float x0, float y0,
                                       float x1, float y1) {
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        x0 < -0.25f || y0 < -0.25f ||
        x1 > 100.25f || y1 > 100.25f ||
        x1 <= x0 || y1 <= y0)
        return BlackFrameKind::Preserve;

    const float width = x1 - x0;
    const float height = y1 - y0;
    // Movie bars are screen-edge strips, not full-screen black fades,
    // loading screens or independently centered 4:3 sidebars.
    const bool full_width = x0 <= 4.0f && x1 >= 96.0f;
    const bool top_bar = y0 <= 1.5f && y1 <= 28.0f;
    const bool bottom_bar = y0 >= 72.0f && y1 >= 98.5f;
    if (full_width && height >= 2.0f && height <= 28.0f &&
        (top_bar || bottom_bar))
        return BlackFrameKind::CinematicLetterbox;

    // Original owner Wine traces observed the native dialogue frame
    // around x=13..88 / y=81..94 and x=14..94 / y=80.6..92.4.
    // Avoid full-screen transitions, inventory/menu center panels,
    // and narrow text glyphs, arrows or decorative edges.
    const bool interior = x0 >= 5.0f && x1 <= 97.0f;
    if (interior && width >= 55.0f && width <= 92.0f &&
        y0 >= 70.0f && y1 <= 97.0f &&
        height >= 5.0f && height <= 25.0f)
        return BlackFrameKind::DialogueBackdrop;

    return BlackFrameKind::Preserve;
}

inline bool remove_black_frame(bool opt_in, bool verified_frame_producer,
                               float x0, float y0, float x1, float y1) {
    return opt_in && verified_frame_producer &&
           black_frame_kind(x0, y0, x1, y1) != BlackFrameKind::Preserve;
}

} // namespace r2fix
