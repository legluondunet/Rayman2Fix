#include "rayman2fix/dialogue_math.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

int main() {
    const auto near=[](float a,float b) {
        return std::fabs(a-b)<0.0001f;
    };
    r2fix::JffTextString32 native{};
    native.text_ptr=0x12340000;
    native.x=150.0f; native.y=805.0f; native.size=10.0f;
    native.alpha=221; native.flags=0x18;
    native.wave=1; native.effect=-15; native.forced_color=7;
    native.padding[0]=0xA5;
    auto a=native;
    assert(r2fix::fit_learn10_dialogue_text(native,0.75f,a));
    // 16:9: the original 150/1000 position must move from the
    // left-of-panel region to the *inside* of the 4:3 dialogue canvas.
    assert(near(a.x,237.5f) && near(a.size,7.5f));
    assert(near(a.y,805.0f));
    assert(a.text_ptr==native.text_ptr && a.alpha==native.alpha);
    assert(a.flags==native.flags && a.wave==native.wave);
    assert(a.effect==native.effect && a.forced_color==native.forced_color);
    assert(a.padding[0]==native.padding[0]);
    // Actual game-owned record is never mutated, even across frames.
    assert(near(native.x,150.0f) && near(native.size,10.0f));
    assert(!r2fix::fit_learn10_dialogue_text(native,1.0f,a));
    assert(!r2fix::fit_learn10_dialogue_text(native,0.0f,a));
    assert(!r2fix::fit_learn10_dialogue_text(native,
                         std::numeric_limits<float>::quiet_NaN(),a));
    auto bad=native;
    bad.y=500.0f; // menus and most HUD text are NOT dialogue
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    bad=native;bad.text_ptr=0;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    bad=native;bad.x=1001.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    // Sizes 18 and 20 are actually used by the native JFFTXT renderer;
    // keep them eligible in the original lower dialogue band.
    bad=native;bad.size=18.0f;
    assert(r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    assert(near(a.size,13.5f));
    bad=native;bad.size=20.0f;
    assert(r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    assert(near(a.size,15.0f));
    bad=native;bad.size=26.0f;
    assert(r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    bad=native;bad.size=26.01f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    bad=native;bad.size=std::numeric_limits<float>::infinity();
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    bad=native;bad.x=std::numeric_limits<float>::quiet_NaN();
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,a));
    // No hardcoded 1920x1080; any wider aspect uses the same ratio.
    a=native;
    assert(r2fix::fit_learn10_dialogue_text(native,0.5625f,a));
    assert(near(a.x,303.125f) && near(a.size,5.625f));
    return 0;
}
