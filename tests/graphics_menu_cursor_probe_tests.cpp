#include <cstdlib>
#include <limits>
#include "rayman2fix/graphics_menu_cursor_probe.hpp"
static void check(bool v){if(!v)std::abort();}
int main(){
 using r2fix::may_be_graphics_menu_cursor;
 check(may_be_graphics_menu_cursor({435,530,155,250}));
 check(may_be_graphics_menu_cursor({540,620,340,410}));
 check(!may_be_graphics_menu_cursor({0,1024,0,768})); // full-screen
 check(!may_be_graphics_menu_cursor({614,768,652,691})); // brightness bar
 check(!may_be_graphics_menu_cursor({150,190,95,125})); // left edge
 check(!may_be_graphics_menu_cursor({250,258,180,230})); // too narrow
 check(!may_be_graphics_menu_cursor({380,440,50,70})); // title region
 const float nan=std::numeric_limits<float>::quiet_NaN();
 check(!may_be_graphics_menu_cursor({nan,530,155,250}));
}
