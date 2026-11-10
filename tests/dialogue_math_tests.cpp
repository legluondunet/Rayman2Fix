#include "rayman2fix/dialogue_math.hpp"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    const auto near=[](float a,float b) {
        return std::fabs(a-b)<0.0001f;
    };
    r2fix::JffTextString32 native{};
    native.text_ptr=0x12340000;
    native.x=500.0f; native.y=500.0f; native.size=20.0f;
    native.alpha=221; native.flags=0;
    native.wave=1; native.effect=-15; native.forced_color=7;
    native.padding[0]=0xA5;
    for (const float y : {500.0f,600.0f,700.0f}) {
        native.y=y;
        auto adjusted=native;
        assert(r2fix::is_learn10_caption_family(native));
        assert(r2fix::fit_learn10_dialogue_text(native,0.75f,adjusted));
        assert(near(adjusted.x,500.0f));
        assert(near(adjusted.y,y));
        assert(near(adjusted.size,15.0f));
        assert(adjusted.text_ptr==native.text_ptr);
        assert(adjusted.alpha==native.alpha);
        assert(adjusted.flags==native.flags);
        assert(adjusted.wave==native.wave);
        assert(adjusted.effect==native.effect);
        assert(adjusted.forced_color==native.forced_color);
        assert(adjusted.padding[0]==native.padding[0]);
        // Never modify engine-owned source descriptors.
        assert(near(native.x,500.0f) && near(native.size,20.0f));
        // Same 4:3 fitting rule for any widescreen aspect, no
        // hardcoded 1920x1080 output-size dependency.
        assert(r2fix::fit_learn10_dialogue_text(native,0.5625f,adjusted));
        assert(near(adjusted.size,11.25f));
        assert(!r2fix::fit_learn10_dialogue_text(native,1.0f,adjusted));
    }
    auto bad=native;
    bad.y=70.0f; // native JFFTXT HUD text
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.y=180.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.y=400.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.y=650.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.y=800.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.x=690.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.x=503.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.size=18.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.size=25.0f;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.flags=1;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.text_ptr=0;
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.x=std::numeric_limits<float>::quiet_NaN();
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    bad=native;bad.size=std::numeric_limits<float>::infinity();
    assert(!r2fix::fit_learn10_dialogue_text(bad,0.75f,bad));
    assert(!r2fix::fit_learn10_dialogue_text(native,0.0f,bad));
    assert(!r2fix::fit_learn10_dialogue_text(native,
                    std::numeric_limits<float>::quiet_NaN(),bad));
    return 0;
}
