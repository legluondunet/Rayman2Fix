#include <cmath>
#include <cstdlib>
#include "rayman2fix/graphics_menu_glyph_layout.hpp"
#include "rayman2fix/native_graphics_info_color.hpp"
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
    // Regression, 23 Nov Options screenshot: normal-3 also produces
    // glyphs on the parent Options page. Geometry-only suppression
    // must fail closed, even if the VIDEO setup flag is true. These
    // apparently familiar row bands cannot prove a VIDEO glyph owner.
    expect(!legacy_resolution_preparation_glyph(
        true,{55.0f,31.0f,60.0f,36.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{47.0f,64.0f,51.0f,69.0f}));
    expect(!legacy_resolution_preparation_glyph(
        false,{55.0f,31.0f,60.0f,36.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{35.3f,20.0f,39.5f,25.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{5.0f,45.0f,9.0f,50.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{48.0f,85.0f,52.0f,90.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{NAN,43.0f,55.0f,50.0f}));
    expect(!legacy_resolution_preparation_glyph(
        true,{55.0f,50.0f,54.0f,55.0f}));
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
    // Wine 23 Nov 2026: the native colored GLI callback provides a
    // 4×ARGB palette, separately from vertex tint. The selected row
    // becomes yellow (0xFFFFFF78), other lines blue/pink. A VIDEO
    // information row must render identically regardless of focus.
    const std::uint32_t samples_font[][4]={
        {0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFF78u,0xFFFFFF78u},
        {0xFFFFFFFFu,0xFFFFFFFFu,0xFF6EBEFFu,0xFF6EBEFFu},
        {0xFFFFFFFFu,0xFFFFFFFFu,0xFFFA4646u,0xFFFA4646u},
        {0x5AFFFFFFu,0x5AFFFFFFu,0x5A6EBEFFu,0x5A6EBEFFu}};
    for(unsigned native_row=1;native_row<=4;++native_row) {
        for(int native_focus=1;native_focus<=4;++native_focus) {
            const auto& palette=samples_font[native_row-1];
            std::uint32_t fixed[4]{};
            expect(static_video_information_palette(
                palette,fixed,true,native_row,native_focus));
            // All four rows are static labels, including the formerly
            // grey/translucent OUTPUT row (old native alpha 0x5A).
            for(unsigned i=0;i<4;++i)expect(fixed[i]==0xFFFFFFFFu);
            for(unsigned i=0;i<4;++i)expect(palette[i]==samples_font[native_row-1][i]);
        }
    }
    const std::uint32_t glyph_colors[4]={
        0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFF78u,0xFFFFFF78u};
    std::uint32_t replacement[4]={1u,2u,3u,4u};
    expect(!static_video_information_palette(
        glyph_colors,replacement,false,1u,1));
    expect(!static_video_information_palette(
        glyph_colors,replacement,true,0u,1));
    expect(!static_video_information_palette(
        glyph_colors,replacement,true,1u,5));
    expect(replacement[0]==1u && replacement[3]==4u);
    const std::uint32_t wrong_pair[4]={
        0xFFFFFFFFu,0xFF888888u,0xFF6EBEFFu,0xFF6EBEFFu};
    expect(!static_video_information_palette(
        wrong_pair,replacement,true,1u,1));
    const std::uint32_t bad_alpha[4]={
        0xFFFFFFFFu,0xFFFFFFFFu,0x5A6EBEFFu,0x5A6EBEFFu};
    expect(!static_video_information_palette(
        bad_alpha,replacement,true,1u,1));
    // Approved Wine trace #1687: the obsolete luminosity meter is a
    // pair of ScreenImage quads, precisely 60..75.6 and 75.6..90
    // (percent), y=85..90. Only these two ORIGINAL producer quads
    // may be suppressed, exclusively on the authenticated read-only
    // information page. The menu's 3D butterfly is a separate route.
    const GraphicsInfoQuad meter_left{60.0f,85.0f,75.6f,90.0f};
    const GraphicsInfoQuad meter_right{75.6f,85.0f,90.0f,90.0f};
    expect(readonly_video_brightness_meter_quad(true,1,meter_left));
    expect(readonly_video_brightness_meter_quad(true,1,meter_right));
    // Regression, real 10:06 user log: the split is 75.30 rather
    // than 75.60. Both halves MUST be suppressed on both visits.
    expect(readonly_video_brightness_meter_quad(
        true,1,{60.0f,85.0f,75.3f,90.0f}));
    expect(readonly_video_brightness_meter_quad(
        true,1,{75.3f,85.0f,90.0f,90.0f}));
    expect(readonly_video_brightness_meter_quad(
        true,1,{60.0f,85.0f,74.8f,90.0f}));
    expect(readonly_video_brightness_meter_quad(
        true,1,{74.8f,85.0f,90.0f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(false,1,meter_left));
    expect(!readonly_video_brightness_meter_quad(true,0,meter_left));
    expect(!readonly_video_brightness_meter_quad(true,2,meter_left));
    expect(!readonly_video_brightness_meter_quad(true,3,meter_right));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{59.0f,85.0f,75.6f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{60.0f,83.0f,75.6f,88.0f}));
    // A varying central split is valid; unrelated geometry is not.
    expect(readonly_video_brightness_meter_quad(
        true,1,{60.0f,85.0f,76.0f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{60.0f,85.0f,69.9f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{80.1f,85.0f,90.0f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{50.0f,85.0f,90.0f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{NAN,85.0f,75.6f,90.0f}));
    expect(!readonly_video_brightness_meter_quad(
        true,1,{60.0f,85.0f,75.6f,NAN}));
    // The VIDEO glyphs, title, butterfly, and all non-source-backed
    // images stay unmodified, even when visually close to the meter.
    // Other menus, brightness slider and 3D butterfly are not font
    // glyphs from the four verified native VIDEO source bands.
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
