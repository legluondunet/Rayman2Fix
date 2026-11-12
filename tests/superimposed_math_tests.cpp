#include "rayman2fix/superimposed_math.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

int main() {
    auto near=[](float a,float b){return std::fabs(a-b)<0.0001f;};
    // The same native equation must work at 4:3, 16:9 and 21:9,
    // without menu-, language-, or level-dependent constants.
    for(const float squeeze:{1.0f,0.75f,0.5625f}) {
        float pos=0.0f;
        assert(r2fix::remap_superimposed_screen_x(0,squeeze,pos));
        assert(near(pos,0.5f-0.5f*squeeze));
        assert(r2fix::remap_superimposed_screen_x(500,squeeze,pos));
        assert(near(pos,0.5f));
        assert(r2fix::remap_superimposed_screen_x(1000,squeeze,pos));
        assert(near(pos,0.5f+0.5f*squeeze));
        assert(r2fix::remap_superimposed_screen_x(230,squeeze,pos));
        // A narrowed 1024px viewport yields the SAME horizontal
        // position as this script mapping, so both must not be used.
        assert(near(pos*1024.0f,
                    (1024.0f-1024.0f*squeeze)/2.0f+
                    230.0f/1000.0f*1024.0f*squeeze));
        float extent=0.0f;
        assert(r2fix::superimposed_uniform_extent(
            768.0f,squeeze,extent));
        assert(near(extent,768.0f/480.0f*squeeze));
    }
    float pos=0.0f,extent=0.0f;
    assert(!r2fix::remap_superimposed_screen_x(
        200,std::numeric_limits<float>::quiet_NaN(),pos));
    assert(!r2fix::remap_superimposed_screen_x(200,0.0f,pos));
    assert(!r2fix::remap_superimposed_screen_x(200,1.1f,pos));
    assert(!r2fix::superimposed_uniform_extent(0.0f,0.75f,extent));
    assert(!r2fix::superimposed_uniform_extent(32768.0f,0.75f,extent));
    assert(!r2fix::superimposed_uniform_extent(768.0f,0.2f,extent));
    assert(!r2fix::superimposed_uniform_extent(
        std::numeric_limits<float>::infinity(),0.75f,extent));
    return 0;
}
