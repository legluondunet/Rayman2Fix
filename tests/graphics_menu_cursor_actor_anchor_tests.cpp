#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "rayman2fix/graphics_menu_cursor_actor_anchor.hpp"
static void check(bool ok){if(!ok)std::abort();}
int main(){
    std::array<std::uint8_t,r2fix::kMenuCursorActorAnchorWindow> names{};
    const auto put=[&](std::size_t offset,const char* name) {
        std::memcpy(names.data()+offset,name,std::strlen(name)+1);
    };
    check(!r2fix::verified_menu_cursor_name_anchor(nullptr,names.size()));
    check(!r2fix::verified_menu_cursor_name_anchor(names.data(),0));
    put(0,"YAM_CurseurMenu_I1");
    check(r2fix::menu_cursor_name_present(names.data(),names.size()));
    check(!r2fix::menu_cursor_name_present(names.data(),3));
    check(!r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()));
    put(48,"popolopoi_menu");
    put(92,"ARG_CurseurMenu");
    put(2260,"YAM_GenMenu_I1");
    check(r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()));
    check(!r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()-1));
    names[93]='X';
    check(!r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()));
    names[93]='R';
    check(r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()));
    names[2261]='Z';
    check(!r2fix::verified_menu_cursor_name_anchor(
        names.data(),names.size()));
    return 0;
}
