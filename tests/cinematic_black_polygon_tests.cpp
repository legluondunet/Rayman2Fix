#include "rayman2fix/cinematic_black_polygon.hpp"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while(false)
int main() {
    using K=r2fix::CinematicBlackPolygon;
    const auto cls=r2fix::classify_cinematic_black_polygon;
    // Exact arguments observed on the owner's 2026-11-15 19:18 run.
    CHECK(cls(0x2082D,0,0,1024,1,1024,768)==K::Upper);
    CHECK(cls(0x2082D,0,0,1024,3,1024,768)==K::Upper);
    CHECK(cls(0x20847,0,763,1024,768,1024,768)==K::Lower);
    CHECK(cls(0x20847,0,758,1024,768,1024,768)==K::Lower);
    // Hypothetical taller bars on the exact same native sites.
    CHECK(cls(0x2082D,0,0,1024,150,1024,768)==K::Upper);
    CHECK(cls(0x20847,0,618,1024,768,1024,768)==K::Lower);
    CHECK(cls(0x2082D,0,0,1024,256,1024,768)==K::Upper);
    CHECK(cls(0x20847,0,512,1024,768,1024,768)==K::Lower);
    // Strictly preserve the native left and right polygon calls.
    CHECK(cls(0x2085E,0,0,34,768,1024,768)==K::Preserve);
    CHECK(cls(0x20878,990,0,1024,768,1024,768)==K::Preserve);
    // Wrong native source, even when its bounds mimic a top/bottom bar.
    CHECK(cls(0x2085E,0,0,1024,34,1024,768)==K::Preserve);
    CHECK(cls(0x20878,0,618,1024,768,1024,768)==K::Preserve);
    CHECK(cls(0x2A1BE,0,0,1024,34,1024,768)==K::Preserve);
    // Full-screen fade, central UI masks, large unknown masks,
    // negative coordinates, swapped or zero-area ranges.
    CHECK(cls(0x2082D,0,0,1024,768,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,0,1024,768,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,0,1024,300,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,400,1024,768,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,20,1024,30,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,700,1024,750,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,5,0,1024,34,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,618,900,768,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,0,1024,34,800,600)==K::Preserve);
    CHECK(cls(0x20847,0,0,1024,34,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,0,1024,0,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,768,1024,768,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,-1,1024,34,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,730,1024,769,1024,768)==K::Preserve);
    return 0;
}
