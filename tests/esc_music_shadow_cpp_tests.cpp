// Cross-platform C++17 policy tests: no game code, copyrighted assets or audio.
#include "rayman2fix/esc_music_shadow.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Never use plain assert(): MSVC Release (/DNDEBUG) would silently disable
// every test including setup calls, giving meaningless green CI.
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "CHECK failed " << __FILE__ << ':' << __LINE__ \
              << " : " #expr "\n"; std::abort(); \
} } while(false)

using namespace r2fix::esc_music;
namespace {
constexpr Generation kMusic{9,5,9,7,812};
constexpr Proof kAll{true,true,true,true,true,true};
Shadow fresh() {
    Shadow s;
    CHECK(s.attach(kMusic,kAll));
    return s;
}
void gates() {
    for(unsigned n=0;n<6;++n) {
        Proof p=kAll;
        if(n==0)p.current_level_music=false;
        if(n==1)p.esc_lifetime=false;
        if(n==2)p.mixer_adapter=false;
        if(n==3)p.initial_driver_playing=false;
        if(n==4)p.focus_known=false;
        if(n==5)p.foreground=false;
        Shadow s;
        CHECK(!s.attach(kMusic,p));
        CHECK(s.callback(kMusic,Kind::pause,Route::theme,Cause::esc)==Action::forward_native);
    }
    Shadow invalid;
    CHECK(!invalid.attach(Generation{9,5,9,7,0},kAll));
    CHECK(!invalid.attach(Generation{65536,5,9,7,42},kAll));
}
void two_routes() {
    auto s=fresh();
    CHECK(s.menu_open(true)==Action::none);
    CHECK(s.callback(kMusic,Kind::pause,Route::theme,Cause::esc)==Action::suppress_paired_native);
    CHECK(s.callback(kMusic,Kind::pause,Route::sxd,Cause::esc)==Action::suppress_paired_native);
    CHECK(!s.logical_paused());
    CHECK(s.menu_close(true)==Action::none);
    CHECK(s.callback(kMusic,Kind::resume,Route::sxd,Cause::esc)==Action::suppress_paired_native);
    CHECK(s.callback(kMusic,Kind::resume,Route::theme,Cause::esc)==Action::suppress_paired_native);
    CHECK(s.observed_count(Kind::pause,Route::sxd)==1);
    CHECK(s.observed_count(Kind::resume,Route::theme)==1);
}
void nesting() {
    auto s=fresh();
    s.menu_open(true);
    CHECK(s.focus(true,false)==Action::pause);
    CHECK(s.other_pause()==Action::none);
    CHECK(s.menu_close(true)==Action::none);
    CHECK(s.focus(true,true)==Action::none);
    CHECK(s.logical_paused());
    CHECK(s.other_resume()==Action::resume);
    CHECK(!s.logical_paused());
}
void stop_generation() {
    auto s=fresh();
    s.menu_open(true);
    s.focus(true,false);
    CHECK(s.stop(Generation{9,5,9,7,811})==Action::forward_stop);
    CHECK(s.active());
    CHECK(s.stop(kMusic)==Action::forward_stop);
    CHECK(!s.active());
    CHECK(s.focus(true,true)==Action::none);
    CHECK(s.menu_close(true)==Action::forward_native);
    CHECK(s.callback(kMusic,Kind::resume,Route::theme,Cause::esc)==Action::forward_native);
    CHECK(!s.attach(Generation{9,5,9,7,813},Proof{}));
    CHECK(s.attach(Generation{9,5,9,7,813},kAll));
}
void unknown_is_quarantine() {
    auto s=fresh();
    s.menu_open(true);
    CHECK(s.callback(kMusic,Kind::pause,Route::theme,Cause::unknown)==Action::hold_unknown);
    CHECK(s.quarantined() && s.logical_paused());
    s.focus(true,false);
    s.focus(true,true);
    CHECK(s.menu_close(true)==Action::none);
    CHECK(s.logical_paused());
    CHECK(s.callback(kMusic,Kind::resume,Route::sxd,Cause::esc)==Action::hold_unknown);
    CHECK(s.other_resume()==Action::hold_unknown);
    CHECK(s.logical_paused());
}
void unexpected_callback_order_quarantines() {
    auto s=fresh();
    // Generic script audio pause is not an ESC marker.
    CHECK(s.callback(kMusic,Kind::pause,Route::theme,Cause::esc)==Action::hold_unknown);
    CHECK(s.logical_paused());
}
void enumerate() {
    constexpr std::array<unsigned,8> alphabet{{0,1,2,3,4,5,6,7}};
    std::uint32_t examined=0;
    // All 8^5 = 32,768 event sequences. Never logically play when unfocused,
    // another native pause remains, or the owner is quarantined.
    for(unsigned n=0;n<32768;++n) {
        auto s=fresh();
        unsigned bits=n;
        for(unsigned t=0;t<5;++t) {
            const unsigned ev=alphabet[bits&7u];bits>>=3u;
            switch(ev) {
                case 0:s.menu_open(true);break;
                case 1:s.menu_close(true);break;
                case 2:s.focus(true,false);break;
                case 3:s.focus(true,true);break;
                case 4:s.other_pause();break;
                case 5:s.other_resume();break;
                case 6:s.unknown();break;
                default:s.stop(kMusic);break;
            }
            if(s.active() && (!s.foreground() || s.other_depth()!=0 || s.quarantined()))
                CHECK(s.logical_paused());
            if(!s.active())CHECK(!s.logical_paused());
        }
        ++examined;
    }
    CHECK(examined==32768);
}
}
int main() {
    gates();two_routes();nesting();stop_generation();unknown_is_quarantine();
    unexpected_callback_order_quarantines();enumerate();
    std::cout<<"ESC shadow C++ policy: 32768 sequences + edge cases OK\n";
}
