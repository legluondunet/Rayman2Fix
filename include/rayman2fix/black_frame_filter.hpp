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

    // Mapmonde tutorial: owner log 20261124-223631 reports the exact
    // GLI Frame source at 14..94 / 61.60..73.40, matching the screenshot
    // after the centered 4:3 transform. Do not lower the broad dialogue
    // threshold: unrelated UI panels can occupy the same vertical band.
    const auto frame_coord_matches=[](float a,float b){return std::fabs(a-b)<=0.15f;};
    if(frame_coord_matches(x0,14.f) && frame_coord_matches(x1,94.f) &&
       frame_coord_matches(y0,61.6f) && frame_coord_matches(y1,73.4f))
        return BlackFrameKind::DialogueBackdrop;
    return BlackFrameKind::Preserve;
}

// Broad, read-only source geometry classification for investigating cutscene
// bands. These candidates can be ordinary UI; never suppress using this test.
enum class CinematicEdgeCandidate { None, Upper, Lower };
inline CinematicEdgeCandidate cinematic_edge_candidate(
    float x0, float y0, float x1, float y1) {
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        x0 < -1.0f || x1 > 101.0f || y0 < -1.0f || y1 > 101.0f ||
        x1 <= x0 || y1 <= y0 || x1-x0 < 55.0f ||
        y1-y0 < 1.0f || y1-y0 > 43.0f)
        return CinematicEdgeCandidate::None;
    if (y0 <= 30.0f && y1 <= 48.0f)
        return CinematicEdgeCandidate::Upper;
    if (y0 >= 52.0f && y1 >= 70.0f)
        return CinematicEdgeCandidate::Lower;
    return CinematicEdgeCandidate::None;
}

inline bool remove_black_frame(bool remove_dialogue_backgrounds,
                               bool remove_cinematic_bars,
                               bool verified_frame_producer,
                               float x0, float y0, float x1, float y1) {
    if (!verified_frame_producer) return false;
    switch (black_frame_kind(x0, y0, x1, y1)) {
        case BlackFrameKind::DialogueBackdrop:
            return remove_dialogue_backgrounds;
        case BlackFrameKind::CinematicLetterbox:
            return remove_cinematic_bars;
        default:
            return false;
    }
}

} // namespace r2fix
