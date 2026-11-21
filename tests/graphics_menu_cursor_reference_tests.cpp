#include <cstdlib>
#include <cstdint>
#include "rayman2fix/graphics_menu_cursor_reference_model.hpp"
static void check(bool x){if(!x)std::abort();}
int main() {
    constexpr std::uintptr_t anchor=0x079001BCu;
    check(r2fix::menu_cursor_reference_kind(anchor,anchor)==1);
    check(r2fix::menu_cursor_reference_kind(anchor+92u,anchor)==2);
    check(r2fix::menu_cursor_reference_kind(anchor+2260u,anchor)==3);
    check(r2fix::menu_cursor_reference_kind(anchor-444u,anchor)==4);
    check(r2fix::menu_cursor_reference_kind(anchor+48u,anchor)==0);
    check(r2fix::menu_cursor_reference_kind(0,anchor)==0);
    check(r2fix::menu_cursor_reference_kind(0x10000u,0)==0);
    check(r2fix::menu_cursor_reference_kind(0,0xFFFFFFFFu)==0);
    check(r2fix::menu_cursor_reference_site_near_anchor(anchor+20,anchor));
    check(r2fix::menu_cursor_reference_site_near_anchor(anchor-300,anchor));
    check(!r2fix::menu_cursor_reference_site_near_anchor(anchor+70000,anchor));
    check(!r2fix::menu_cursor_reference_site_near_anchor(0,anchor));
    return 0;
}
