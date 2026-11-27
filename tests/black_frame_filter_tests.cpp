#include "rayman2fix/black_frame_filter.hpp"

#include <cstdlib>
#include <limits>

#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)

int main() {
    using K = r2fix::BlackFrameKind;
    const auto cls = r2fix::black_frame_kind;
    // Actual geometries reported by the owner's earlier Wine logs for
    // the GLI frame producer behind native lower-screen dialogue.
    CHECK(cls(13.0f,81.0f,88.0f,94.0f) == K::DialogueBackdrop);
    CHECK(cls(14.0f,80.6f,94.0f,92.4f) == K::DialogueBackdrop);

    // Mapmonde tutorial screenshot + original GLI frame trace.
    CHECK(cls(14.f,61.6f,94.f,73.4f)==K::DialogueBackdrop);
    CHECK(cls(14.05f,61.65f,93.95f,73.35f)==K::DialogueBackdrop);
    CHECK(cls(14.f,60.f,94.f,74.f)==K::Preserve);
    CHECK(cls(10.f,61.6f,90.f,73.4f)==K::Preserve);
    CHECK(!r2fix::remove_black_frame(false,true,true,14,61.6f,94,73.4f));
    CHECK(!r2fix::remove_black_frame(true,false,false,14,61.6f,94,73.4f));
    CHECK(r2fix::remove_black_frame(true,false,true,14,61.6f,94,73.4f));

    // Single-line Mapmonde tutorial: exact trace, not a general geometry rule.
    CHECK(cls(14.f,64.3f,94.f,70.7f)==K::Preserve);
    CHECK(r2fix::map_single_line_dialogue(true,true,14.f,64.3f,94.f,70.7f));
    CHECK(r2fix::map_single_line_dialogue(true,true,14.05f,64.35f,93.95f,70.65f));
    CHECK(!r2fix::map_single_line_dialogue(false,true,14.f,64.3f,94.f,70.7f));
    CHECK(!r2fix::map_single_line_dialogue(true,false,14.f,64.3f,94.f,70.7f));
    CHECK(!r2fix::map_single_line_dialogue(true,true,14.f,64.f,94.f,70.7f));
    CHECK(!r2fix::map_single_line_dialogue(true,true,0.f,0.f,100.f,100.f));
    CHECK(!r2fix::map_single_line_dialogue(true,true,54.f,76.f,60.f,84.f));
    CHECK(!r2fix::map_single_line_dialogue(true,true,14.f,std::numeric_limits<float>::quiet_NaN(),94.f,70.7f));

    // Plausible engine-drawn cinematic top and bottom letterbox strips.
    // Confirmation of those strips in a live cinematic is still needed.
    CHECK(cls(0.0f,0.0f,100.0f,15.0f) == K::CinematicLetterbox);
    CHECK(cls(0.0f,85.0f,100.0f,100.0f) == K::CinematicLetterbox);
    CHECK(cls(3.5f,1.0f,96.5f,18.0f) == K::CinematicLetterbox);
    CHECK(cls(2.0f,81.0f,99.0f,100.0f) == K::CinematicLetterbox);

    // Black fades, loading screens, menus, central frame shapes,
    // fonts, skinny sidebars and arbitrary out-of-range coordinates.
    CHECK(cls(0,0,100,100) == K::Preserve);
    CHECK(cls(0,5,100,95) == K::Preserve);
    CHECK(cls(10,10,90,80) == K::Preserve);
    CHECK(cls(5,40,95,60) == K::Preserve);
    CHECK(cls(0,0,18,100) == K::Preserve);
    CHECK(cls(82,0,100,100) == K::Preserve);
    CHECK(cls(30,80,40,95) == K::Preserve);
    CHECK(cls(0,98.0f,100,100) == K::CinematicLetterbox);
    CHECK(cls(0,99.0f,100,100) == K::Preserve);
    CHECK(cls(14,70,94,98) == K::Preserve);
    CHECK(cls(13,80,88,100) == K::Preserve);
    CHECK(cls(13,80,13,94) == K::Preserve);
    CHECK(cls(13,90,88,80) == K::Preserve);
    CHECK(cls(-10,80,90,95) == K::Preserve);
    CHECK(cls(12,80,110,95) == K::Preserve);
    CHECK(cls(14,-20,94,10) == K::Preserve);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(cls(nan,80,90,95) == K::Preserve);
    CHECK(cls(13,inf,90,95) == K::Preserve);
    CHECK(cls(13,80,90,-inf) == K::Preserve);

    // The broader read-only candidate test does not authorize removal.
    using E = r2fix::CinematicEdgeCandidate;
    CHECK(r2fix::cinematic_edge_candidate(0,0,100,15)==E::Upper);
    CHECK(r2fix::cinematic_edge_candidate(5,5,75,22)==E::Upper);
    CHECK(r2fix::cinematic_edge_candidate(0,85,100,100)==E::Lower);
    CHECK(r2fix::cinematic_edge_candidate(14,80,94,92)==E::Lower);
    CHECK(r2fix::cinematic_edge_candidate(0,0,100,100)==E::None);
    CHECK(r2fix::cinematic_edge_candidate(0,48,100,52)==E::None);
    CHECK(r2fix::cinematic_edge_candidate(0,90,40,99)==E::None);
    CHECK(r2fix::cinematic_edge_candidate(nan,0,100,5)==E::None);

    // Both opt-in AND source identity are required, independently of aspect.
    // Each opt-in controls its own class, never the other class.
    CHECK(!r2fix::remove_black_frame(false,false,true,13,81,88,94));
    CHECK(!r2fix::remove_black_frame(true,true,false,13,81,88,94));
    CHECK(r2fix::remove_black_frame(true,false,true,13,81,88,94));
    CHECK(!r2fix::remove_black_frame(true,false,true,0,0,100,15));
    CHECK(!r2fix::remove_black_frame(false,true,true,13,81,88,94));
    CHECK(r2fix::remove_black_frame(false,true,true,0,0,100,15));
    CHECK(r2fix::remove_black_frame(true,true,true,0,85,100,100));
    CHECK(!r2fix::remove_black_frame(true,true,true,0,0,100,100));
    CHECK(!r2fix::remove_black_frame(true,true,true,10,40,90,60));
    return 0;
}
