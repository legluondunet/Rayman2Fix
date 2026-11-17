#include "rayman2fix/superimposed_math.hpp"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <limits>

// Explicitly check in both Debug and Release (assert is disabled by NDEBUG).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::abort(); \
    } \
} while (false)


int main() {
    auto near=[](float a,float b){return std::fabs(a-b)<0.0001f;};
    // The same native equation must work at 4:3, 16:9 and 21:9,
    // without menu-, language-, or level-dependent constants.
    for(const float squeeze:{1.0f,0.75f,0.5625f}) {
        float pos=0.0f;
        CHECK(r2fix::remap_superimposed_screen_x(0,squeeze,pos));
        CHECK(near(pos,0.5f-0.5f*squeeze));
        CHECK(r2fix::remap_superimposed_screen_x(500,squeeze,pos));
        CHECK(near(pos,0.5f));
        CHECK(r2fix::remap_superimposed_screen_x(1000,squeeze,pos));
        CHECK(near(pos,0.5f+0.5f*squeeze));
        CHECK(r2fix::remap_superimposed_screen_x(230,squeeze,pos));
        // A narrowed 1024px viewport yields the SAME horizontal
        // position as this script mapping, so both must not be used.
        CHECK(near(pos*1024.0f,
                    (1024.0f-1024.0f*squeeze)/2.0f+
                    230.0f/1000.0f*1024.0f*squeeze));
        float horizontal=0.0f,vertical=0.0f;
        CHECK(r2fix::superimposed_text_extent_coefficients(
            squeeze,horizontal,vertical));
        CHECK(near(horizontal,(1.0f/640.0f)*squeeze));
        CHECK(near(vertical,1.0f/480.0f));
        // The vertical scale must not change between 4:3, 16:9 and
        // 21:9. This guards against the screenshot-confirmed 0.75x
        // cinematic title flattening on a 1920x1080 monitor.
        CHECK(near(768.0f*vertical,768.0f/480.0f));
        if(near(squeeze,0.75f)) {
            CHECK(!near(vertical,(1.0f/480.0f)*squeeze));
        }
        float actor_horizontal=0.0f,actor_restore_y=0.0f;
        CHECK(r2fix::superimposed_actor_extent_coefficients(
            squeeze,actor_horizontal,actor_restore_y));
        CHECK(near(actor_horizontal,(1.0f/640.0f)*squeeze));
        CHECK(near(actor_restore_y,1.0f/squeeze));
        const float scene_width=1024.0f;
        const float actor_uniform_value=scene_width*actor_horizontal;
        CHECK(near(actor_uniform_value*actor_restore_y,
                    scene_width/640.0f));
        if(near(squeeze,0.75f)) {
            CHECK(near(actor_uniform_value,1.2f));
            CHECK(near(actor_uniform_value*actor_restore_y,1.6f));
        }
        float extent=0.0f;
        CHECK(r2fix::superimposed_uniform_extent(
            768.0f,squeeze,extent));
        CHECK(near(extent,768.0f/480.0f*squeeze));
    }
    // All four approved retail CD callers of GLI_vGet3DVertexFromScreenPos
    // use this X normalization (actor 409, independent actor route,
    // two native 3D text branches).
    for(const float squeeze:{1.0f,0.75f,0.5625f}) {
        for(const float x:{0.0f,0.23f,0.5f,0.77f,1.0f}) {
            float centered=0.0f;
            CHECK(r2fix::center_normalized_superimposed_x(
                x,squeeze,centered));
            CHECK(near(centered,0.5f+(x-0.5f)*squeeze));
            // Must be equivalent to a centered viewport for position
            // but never applied IN ADDITION to such a viewport.
            CHECK(near(centered*1024.0f,
                (1024.0f*(1.0f-squeeze)/2.0f)+x*1024.0f*squeeze));
        }
    }
    // Both original retail x86 sites use exactly 6 bytes and may have
    // different base registers. Validate the generated gateway bytes
    // and x87 FMUL operand; guard against a future wrong-register patch.
    constexpr std::uint32_t factor=0x1234ABCDu;
    constexpr std::uint32_t gateway_va=0x30001000u;
    r2fix::SuperimposedActorStoreGateway ed{},es{};
    CHECK(r2fix::make_superimposed_actor_store_gateway(
        true,factor,gateway_va,0x471236u,ed));
    CHECK(r2fix::make_superimposed_actor_store_gateway(
        false,factor,gateway_va,0x4718DCu,es));
    for(const auto* code:{&ed,&es}) {
        CHECK(code->bytes.size()==17u);
        CHECK(code->bytes[0]==0xD9u && code->bytes[2]==0x34u);
        CHECK(code->bytes[3]==0xD8u && code->bytes[4]==0x0Du);
        CHECK(code->bytes[9]==0xD9u && code->bytes[11]==0x54u);
        CHECK(code->bytes[12]==0xE9u);
        std::uint32_t operand=0,relative=0;
        std::memcpy(&operand,code->bytes.data()+5,4);
        std::memcpy(&relative,code->bytes.data()+13,4);
        CHECK(operand==factor);
        CHECK(gateway_va+17u+relative==
            (code==&ed ? 0x471236u : 0x4718DCu));
    }
    CHECK(ed.bytes[1]==0x57u && ed.bytes[10]==0x5Fu);
    CHECK(es.bytes[1]==0x56u && es.bytes[10]==0x5Eu);
    CHECK(!r2fix::make_superimposed_actor_store_gateway(
        true,0u,gateway_va,0x471236u,ed));
    float pos=0.0f,extent=0.0f;
    CHECK(!r2fix::center_normalized_superimposed_x(
        std::numeric_limits<float>::quiet_NaN(),0.75f,pos));
    CHECK(!r2fix::center_normalized_superimposed_x(0.5f,0.0f,pos));
    CHECK(!r2fix::center_normalized_superimposed_x(0.5f,1.1f,pos));
    CHECK(!r2fix::center_normalized_superimposed_x(200.0f,0.75f,pos));
    CHECK(!r2fix::remap_superimposed_screen_x(
        200,std::numeric_limits<float>::quiet_NaN(),pos));
    CHECK(!r2fix::remap_superimposed_screen_x(200,0.0f,pos));
    CHECK(!r2fix::remap_superimposed_screen_x(200,1.1f,pos));
    float hor=0.0f,ver=0.0f;
    CHECK(!r2fix::superimposed_text_extent_coefficients(
        std::numeric_limits<float>::quiet_NaN(),hor,ver));
    CHECK(!r2fix::superimposed_text_extent_coefficients(
        0.0f,hor,ver));
    CHECK(!r2fix::superimposed_text_extent_coefficients(
        1.1f,hor,ver));
    CHECK(!r2fix::superimposed_text_extent_coefficients(
        0.2f,hor,ver));
    CHECK(r2fix::superimposed_text_extent_coefficients(
        1.0f,hor,ver));
    CHECK(near(hor,1.0f/640.0f) && near(ver,1.0f/480.0f));
    float actor_x=0.0f,actor_vertical=0.0f;
    CHECK(!r2fix::superimposed_actor_extent_coefficients(
        std::numeric_limits<float>::quiet_NaN(),actor_x,actor_vertical));
    CHECK(!r2fix::superimposed_actor_extent_coefficients(
        0.0f,actor_x,actor_vertical));
    CHECK(!r2fix::superimposed_actor_extent_coefficients(
        1.1f,actor_x,actor_vertical));
    CHECK(r2fix::superimposed_actor_extent_coefficients(
        1.0f,actor_x,actor_vertical));
    CHECK(near(actor_x,1.0f/640.0f) && near(actor_vertical,1.0f));
    CHECK(!r2fix::superimposed_uniform_extent(0.0f,0.75f,extent));
    CHECK(!r2fix::superimposed_uniform_extent(32768.0f,0.75f,extent));
    CHECK(!r2fix::superimposed_uniform_extent(768.0f,0.2f,extent));
    CHECK(!r2fix::superimposed_uniform_extent(
        std::numeric_limits<float>::infinity(),0.75f,extent));
    return 0;
}
