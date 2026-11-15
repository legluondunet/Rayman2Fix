#include "rayman2fix/superimposed_math.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <cstring>
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
        float horizontal=0.0f,vertical=0.0f;
        assert(r2fix::superimposed_text_extent_coefficients(
            squeeze,horizontal,vertical));
        assert(near(horizontal,(1.0f/640.0f)*squeeze));
        assert(near(vertical,1.0f/480.0f));
        // The vertical scale must not change between 4:3, 16:9 and
        // 21:9. This guards against the screenshot-confirmed 0.75x
        // cinematic title flattening on a 1920x1080 monitor.
        assert(near(768.0f*vertical,768.0f/480.0f));
        if(near(squeeze,0.75f)) {
            assert(!near(vertical,(1.0f/480.0f)*squeeze));
        }
        float actor_horizontal=0.0f,actor_restore_y=0.0f;
        assert(r2fix::superimposed_actor_extent_coefficients(
            squeeze,actor_horizontal,actor_restore_y));
        assert(near(actor_horizontal,(1.0f/640.0f)*squeeze));
        assert(near(actor_restore_y,1.0f/squeeze));
        const float scene_width=1024.0f;
        const float actor_uniform_value=scene_width*actor_horizontal;
        assert(near(actor_uniform_value*actor_restore_y,
                    scene_width/640.0f));
        if(near(squeeze,0.75f)) {
            assert(near(actor_uniform_value,1.2f));
            assert(near(actor_uniform_value*actor_restore_y,1.6f));
        }
        float extent=0.0f;
        assert(r2fix::superimposed_uniform_extent(
            768.0f,squeeze,extent));
        assert(near(extent,768.0f/480.0f*squeeze));
    }
    // All four approved retail CD callers of GLI_vGet3DVertexFromScreenPos
    // use this X normalization (actor 409, independent actor route,
    // two native 3D text branches).
    for(const float squeeze:{1.0f,0.75f,0.5625f}) {
        for(const float x:{0.0f,0.23f,0.5f,0.77f,1.0f}) {
            float centered=0.0f;
            assert(r2fix::center_normalized_superimposed_x(
                x,squeeze,centered));
            assert(near(centered,0.5f+(x-0.5f)*squeeze));
            // Must be equivalent to a centered viewport for position
            // but never applied IN ADDITION to such a viewport.
            assert(near(centered*1024.0f,
                (1024.0f*(1.0f-squeeze)/2.0f)+x*1024.0f*squeeze));
        }
    }
    // Both original retail x86 sites use exactly 6 bytes and may have
    // different base registers. Validate the generated gateway bytes
    // and x87 FMUL operand; guard against a future wrong-register patch.
    constexpr std::uint32_t factor=0x1234ABCDu;
    constexpr std::uint32_t gateway_va=0x30001000u;
    r2fix::SuperimposedActorStoreGateway ed{},es{};
    assert(r2fix::make_superimposed_actor_store_gateway(
        true,factor,gateway_va,0x471236u,ed));
    assert(r2fix::make_superimposed_actor_store_gateway(
        false,factor,gateway_va,0x4718DCu,es));
    for(const auto* code:{&ed,&es}) {
        assert(code->bytes.size()==17u);
        assert(code->bytes[0]==0xD9u && code->bytes[2]==0x34u);
        assert(code->bytes[3]==0xD8u && code->bytes[4]==0x0Du);
        assert(code->bytes[9]==0xD9u && code->bytes[11]==0x54u);
        assert(code->bytes[12]==0xE9u);
        std::uint32_t operand=0,relative=0;
        std::memcpy(&operand,code->bytes.data()+5,4);
        std::memcpy(&relative,code->bytes.data()+13,4);
        assert(operand==factor);
        assert(gateway_va+17u+relative==
            (code==&ed ? 0x471236u : 0x4718DCu));
    }
    assert(ed.bytes[1]==0x57u && ed.bytes[10]==0x5Fu);
    assert(es.bytes[1]==0x56u && es.bytes[10]==0x5Eu);
    assert(!r2fix::make_superimposed_actor_store_gateway(
        true,0u,gateway_va,0x471236u,ed));
    float pos=0.0f,extent=0.0f;
    assert(!r2fix::center_normalized_superimposed_x(
        std::numeric_limits<float>::quiet_NaN(),0.75f,pos));
    assert(!r2fix::center_normalized_superimposed_x(0.5f,0.0f,pos));
    assert(!r2fix::center_normalized_superimposed_x(0.5f,1.1f,pos));
    assert(!r2fix::center_normalized_superimposed_x(200.0f,0.75f,pos));
    assert(!r2fix::remap_superimposed_screen_x(
        200,std::numeric_limits<float>::quiet_NaN(),pos));
    assert(!r2fix::remap_superimposed_screen_x(200,0.0f,pos));
    assert(!r2fix::remap_superimposed_screen_x(200,1.1f,pos));
    float hor=0.0f,ver=0.0f;
    assert(!r2fix::superimposed_text_extent_coefficients(
        std::numeric_limits<float>::quiet_NaN(),hor,ver));
    assert(!r2fix::superimposed_text_extent_coefficients(
        0.0f,hor,ver));
    assert(!r2fix::superimposed_text_extent_coefficients(
        1.1f,hor,ver));
    assert(!r2fix::superimposed_text_extent_coefficients(
        0.2f,hor,ver));
    assert(r2fix::superimposed_text_extent_coefficients(
        1.0f,hor,ver));
    assert(near(hor,1.0f/640.0f) && near(ver,1.0f/480.0f));
    float actor_x=0.0f,actor_vertical=0.0f;
    assert(!r2fix::superimposed_actor_extent_coefficients(
        std::numeric_limits<float>::quiet_NaN(),actor_x,actor_vertical));
    assert(!r2fix::superimposed_actor_extent_coefficients(
        0.0f,actor_x,actor_vertical));
    assert(!r2fix::superimposed_actor_extent_coefficients(
        1.1f,actor_x,actor_vertical));
    assert(r2fix::superimposed_actor_extent_coefficients(
        1.0f,actor_x,actor_vertical));
    assert(near(actor_x,1.0f/640.0f) && near(actor_vertical,1.0f));
    assert(!r2fix::superimposed_uniform_extent(0.0f,0.75f,extent));
    assert(!r2fix::superimposed_uniform_extent(32768.0f,0.75f,extent));
    assert(!r2fix::superimposed_uniform_extent(768.0f,0.2f,extent));
    assert(!r2fix::superimposed_uniform_extent(
        std::numeric_limits<float>::infinity(),0.75f,extent));
    return 0;
}
