#include <cmath>
#include <cstdlib>
#include "rayman2fix/graphics_menu_glyph_layout.hpp"
using namespace r2fix;
void expect(bool ok) { if(!ok)std::abort(); }
int main() {
    GraphicsInfoQuad out{};
    // The VIDEO heading at 20% must remain native and centered.
    // Only the four data-row bands are remapped.
    expect(!layout_graphics_info_quad({35,20,38,25},out));
    expect(layout_graphics_info_quad({47,64,50,69},out));
    expect(out.top>=31 && out.bottom<=35);
    expect(layout_graphics_info_quad({55,34,58,39},out));
    expect(out.top>=39 && out.bottom<45);
    expect(layout_graphics_info_quad({55,45,58,50},out));
    expect(out.top>=50 && out.bottom<57);
    expect(layout_graphics_info_quad({55,55,58,60},out));
    expect(out.top>=61 && out.bottom<68);
    // Brightness, other pages, strange geometry and overlong quads unchanged.
    expect(!layout_graphics_info_quad({8,84,11,89},out));
    expect(!layout_graphics_info_quad({0,80,5,85},out));
    expect(!layout_graphics_info_quad({0,20,400,25},out));
    expect(!layout_graphics_info_quad({22,22,20,27},out));
    expect(!layout_graphics_info_quad({NAN,20,25,25},out));
    expect(!layout_graphics_info_quad({10,4,13,8},out));
    // A long Win32/Wine adapter name is kept in a left column, away
    // from the native selection butterfly (middle of the VIDEO page).
    GraphicsInfoQuad wide{};
    expect(layout_graphics_info_quad({55,34,200,39},wide));
    expect(wide.left>=5.0f && wide.right<55.0f);
    // Both the in-game 4:3 path and the centered 16:9 4:3 canvas
    // must remain valid. centering uses x'=50+(x-50)*0.75.
    const float screen_left=50.0f+(wide.left-50.0f)*0.75f;
    const float screen_right=50.0f+(wide.right-50.0f)*0.75f;
    expect(screen_left>=12.5f && screen_right<55.0f);
    // Reject bizarre off-column glyphs instead of remapping them:
    // the viewport hook must fail back to the untouched native quad.
    expect(!layout_graphics_info_quad({-9,45,210,50},wide));
    // Every data-row first glyph starts in the left column.
    // The unmodified VIDEO heading is deliberately excluded.
    const GraphicsInfoQuad samples[]={
        {55,34,58,39}, {55,45,58,50},
        {55,55,58,60}, {47,64,50,69}};
    for(const auto& src:samples) {
        GraphicsInfoQuad transformed{};
        expect(layout_graphics_info_quad(src,transformed));
        expect(transformed.left>=5 && transformed.left<=12);
        expect(transformed.right<15);
    }
    // The maintainer's 2026-11-22 screenshot showed the D3D9 line
    // ~170 screen pixels to the right of every other information row.
    // Its source anchor was 55%, not the old guessed 8%; equal native
    // X anchors must map to equal screen X across all four rows.
    GraphicsInfoQuad gpu{},driver{},depth{},output{};
    expect(layout_graphics_info_quad({55,34,59,39},gpu));
    expect(layout_graphics_info_quad({55,45,59,50},driver));
    expect(layout_graphics_info_quad({55,55,59,60},depth));
    expect(layout_graphics_info_quad({47,64,51,69},output));
    expect(std::fabs(gpu.left-driver.left)<0.0001f);
    expect(std::fabs(gpu.left-depth.left)<0.0001f);
    expect(std::fabs(gpu.left-output.left)<0.0001f);
    expect(output.top<gpu.top && gpu.top<driver.top && driver.top<depth.top);
    // Read-only material provenance: classify which original widget
    // produced each remapped information glyph, before any source move.
    expect(graphics_info_source_row({55,34,58,39})==1u);
    expect(graphics_info_source_row({55,45,58,50})==2u);
    expect(graphics_info_source_row({55,55,58,60})==3u);
    expect(graphics_info_source_row({47,64,50,69})==4u);
    expect(graphics_info_source_row({35,20,38,25})==0u);
    expect(graphics_info_source_row({8,84,11,89})==0u);
    expect(graphics_info_source_row({NAN,45,50,50})==0u);
    expect(graphics_info_source_row({55,50,54,55})==0u);
    // The raw material diagnostic may log exactly 16 distinct pairs,
    // never outside the four verified VIDEO rows and four native focus
    // values. Reject unknown focus or brightness, with no navigation edits.
    std::uint32_t material_bits=0;
    for(int focus=1;focus<=4;++focus)
        for(unsigned source=1;source<=4;++source) {
            const auto bit=graphics_info_material_sample_bit(source,focus);
            expect(bit!=0u && (material_bits&bit)==0u);
            material_bits|=bit;
        }
    expect(material_bits==0xFFFFu);
    expect(graphics_info_material_sample_bit(0,1)==0u);
    expect(graphics_info_material_sample_bit(5,1)==0u);
    expect(graphics_info_material_sample_bit(1,0)==0u);
    expect(graphics_info_material_sample_bit(1,5)==0u);
    // Wine regression 2026-11-22: the old VIDEO info text was cached
    // and rendered giant after entry into LUMINOSITY. This exact
    // brightness-only glyph mask leaves the original title, brightness
    // label, slider (non-glyph) and butterfly untouched.
    for(const auto& src:samples) {
        expect(cached_video_glyph_in_brightness(true,true,src));
        expect(!cached_video_glyph_in_brightness(false,true,src));
        expect(!cached_video_glyph_in_brightness(true,false,src));
    }
    expect(!cached_video_glyph_in_brightness(true,true,{35,20,39,25}));
    expect(!cached_video_glyph_in_brightness(true,true,{48,84,52,89}));
    expect(!cached_video_glyph_in_brightness(true,true,{5,45,9,50}));
    expect(!cached_video_glyph_in_brightness(true,true,{NAN,45,60,50}));
    expect(!cached_video_glyph_in_brightness(true,true,{50,31,49,34}));
    return 0;
}
