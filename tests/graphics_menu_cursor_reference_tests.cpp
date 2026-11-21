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
    int relative=1234;
    check(r2fix::menu_cursor_relative_reference_offset(
        anchor-64u,anchor,relative));
    check(relative==-64);
    check(r2fix::menu_cursor_relative_reference_offset(
        anchor+96u,anchor,relative));
    check(relative==96);
    check(!r2fix::menu_cursor_relative_reference_offset(
        anchor+65540u,anchor,relative));
    check(relative==96); // rejected site must not change result
    const int likely_table[4]={-100,-96,-88,-80};
    const int scattered[4]={-2000,-96,80,900};
    check(r2fix::menu_cursor_reference_offsets_clustered(likely_table));
    check(!r2fix::menu_cursor_reference_offsets_clustered(scattered));
    check(!r2fix::menu_cursor_reference_offsets_clustered(likely_table,8u));
    // Wine #1532: four local references precede their own targets,
    // rather than forming one shared 256-byte actor-pointer table.
    const int observed_local_sites[4]={-12,80,2248,-456};
    for(unsigned kind=1;kind<=4;++kind) {
        check(r2fix::menu_cursor_is_inline_reference(
            observed_local_sites[kind-1u],kind));
        check(r2fix::menu_cursor_is_inline_reference_site(
            anchor+observed_local_sites[kind-1u],anchor,kind));
        check(!r2fix::menu_cursor_is_inline_reference(
            observed_local_sites[kind-1u]+4,kind));
    }
    check(!r2fix::menu_cursor_is_inline_reference(-12,0));
    check(!r2fix::menu_cursor_is_inline_reference(-12,5));
    check(r2fix::menu_cursor_reference_site_gap(anchor+80u,anchor+92u)==12u);
    check(r2fix::menu_cursor_reference_site_gap(anchor-12u,anchor+80u)==92u);
    check(r2fix::menu_cursor_reference_site_gap(anchor+5u,anchor+5u)==0u);
    return 0;
}
