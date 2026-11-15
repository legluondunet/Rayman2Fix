#include "rayman2fix/cinematic_black_polygon.hpp"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while(false)
int main() {
    using r2fix::CinematicBlackPolygon;
    const auto cls=r2fix::classify_cinematic_black_polygon;
    using K=CinematicBlackPolygon;
    // The four native CALL instructions return at 0x42082D, 847, 85E, 878.
    CHECK(cls(0x2085E,0,0,34,1024,1024,768)==K::Upper);
    CHECK(cls(0x20878,618,0,768,1024,1024,768)==K::Lower);
    CHECK(cls(0x2085E,0,0,1,1024,1024,768)==K::Upper);
    CHECK(cls(0x20878,767,0,768,1024,1024,768)==K::Lower);
    // Left/right rectangles, even if they have a similar appearance,
    // must never be suppressed. Preserve all unsupported producer sites.
    CHECK(cls(0x2082D,0,0,768,128,1024,768)==K::Preserve);
    CHECK(cls(0x20847,0,896,768,1024,1024,768)==K::Preserve);
    CHECK(cls(0x2082D,0,0,34,1024,1024,768)==K::Preserve);
    CHECK(cls(0x20847,618,0,768,1024,1024,768)==K::Preserve);
    CHECK(cls(0x2A1BE,0,0,34,1024,1024,768)==K::Preserve);
    // Never suppress full-screen masks, central curtains, unusual
    // output sizes, negative or inverted bounds.
    CHECK(cls(0x2085E,0,0,768,1024,1024,768)==K::Preserve);
    CHECK(cls(0x20878,0,0,768,1024,1024,768)==K::Preserve);
    CHECK(cls(0x2085E,0,0,300,1024,1024,768)==K::Preserve);
    CHECK(cls(0x20878,400,0,768,1024,1024,768)==K::Preserve);
    CHECK(cls(0x2085E,0,128,34,896,1024,768)==K::Preserve);
    CHECK(cls(0x20878,618,0,768,900,1024,768)==K::Preserve);
    CHECK(cls(0x2085E,0,0,34,1024,800,600)==K::Preserve);
    CHECK(cls(0x2085E,0,0,-1,1024,1024,768)==K::Preserve);
    CHECK(cls(0x20878,769,0,768,1024,1024,768)==K::Preserve);
    return 0;
}
