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

    // Both opt-in AND source identity are required, independently of aspect.
    CHECK(!r2fix::remove_black_frame(false,true,13,81,88,94));
    CHECK(!r2fix::remove_black_frame(true,false,13,81,88,94));
    CHECK(r2fix::remove_black_frame(true,true,13,81,88,94));
    CHECK(r2fix::remove_black_frame(true,true,0,0,100,15));
    CHECK(!r2fix::remove_black_frame(true,true,0,0,100,100));
    CHECK(!r2fix::remove_black_frame(true,true,10,40,90,60));
    return 0;
}
