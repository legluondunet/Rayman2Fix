#include "rayman2fix/cinematic_viewport_fill.hpp"
#include <cstdlib>
#define CHECK(expr) do { if(!(expr))std::abort(); } while(false)
int main() {
    const auto calc=r2fix::calculate_cinematic_viewport_fill;
    // Both actual HUD scales must enable the SAME vertical cinematic fix:
    // 0.75 on a 16:9 desktop, 1.0 in centered 4:3 output.
    CHECK(r2fix::should_enable_cinematic_viewport_fill(true,0.75f));
    CHECK(r2fix::should_enable_cinematic_viewport_fill(true,1.0f));
    CHECK(!r2fix::should_enable_cinematic_viewport_fill(false,0.75f));
    CHECK(!r2fix::should_enable_cinematic_viewport_fill(false,1.0f));
    // Source coefficients inferred from the 1024x768 UV inset: 34..618.
    const auto video=calc(1024,768,0,1000,44,805);
    CHECK(video.change && video.top==0 && video.bottom==1000);
    CHECK(calc(1024,768,0,1000,1,999).change);
    CHECK(calc(1024,768,0,1000,0,850).change);
    CHECK(calc(1024,768,0,1000,150,1000).change);
    // Preserve a viewport that already fills the full screen.
    CHECK(!calc(1024,768,0,1000,0,1000).change);
    // Preserve side/central UI panels, out-of-range and tall masks.
    CHECK(!calc(1024,768,100,900,44,805).change);
    CHECK(!calc(1024,768,1,1000,44,805).change);
    CHECK(!calc(1024,768,0,999,44,805).change);
    CHECK(!calc(1024,768,0,1000,400,600).change);
    CHECK(!calc(1024,768,0,1000,30,500).change);
    CHECK(!calc(1024,768,0,1000,-1,850).change);
    CHECK(!calc(1024,768,0,1000,20,1200).change);
    CHECK(!calc(1280,720,0,1000,44,805).change);
    CHECK(!calc(1024,600,0,1000,44,805).change);
    CHECK(!calc(320,200,0,1000,44,805).change);
    CHECK(!calc(1024,768,0,1000,900,850).change);
    return 0;
}
