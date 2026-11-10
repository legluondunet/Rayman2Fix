#pragma once
#include "rayman2fix/fov_math.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
namespace r2fix {
// The game owns one dedicated camera for projected UI objects, including
// 3D glyphs. Read its identity from the validated, native global slot;
// do not guess UI by level name, font metrics or screen coordinates.
inline bool is_native_screen_camera(const void* candidate,
                                    const void* ui_camera) {
    return candidate!=nullptr && ui_camera!=nullptr &&
           candidate==ui_camera;
}
inline float projection_alpha_x(float original_x,float alpha_y,
                                float aspect,bool native_ui) {
    if(native_ui || !std::isfinite(original_x) || aspect<=1.334f)
        return original_x;
    const float wide=hor_plus_fov(alpha_y,aspect);
    return std::isfinite(wide) && wide>0.05f && wide<3.0f
        ? wide : original_x;
}
// The game engine's GLI viewport copy is 0x70 bytes. These four
// fields are read by GLI_xAdjustCameraToViewport2 (VA 0x422AB0).
// Keep the original viewport intact; the caller passes a stack copy
// so we can let the engine rebuild a self-consistent projection.
struct ScreenWorldViewport {
    std::int32_t width=0;
    std::int32_t height=0;
    std::int32_t left=0;
    std::int32_t top=0;
};
inline bool center_screen_world_viewport(
        const ScreenWorldViewport& current,float scale,
        ScreenWorldViewport& result) {
    if(!std::isfinite(scale) || scale<0.25f || scale>=0.9999f ||
       current.width<128 || current.width>16384 ||
       current.height<128 || current.height>16384 ||
       current.left< -16384 || current.left>16384 ||
       current.top< -16384 || current.top>16384)
        return false;
    const long width=std::lround(
        static_cast<double>(current.width)*scale);
    if(width<128 || width>=current.width) return false;
    const std::int64_t x=static_cast<std::int64_t>(current.left)+
        (static_cast<std::int64_t>(current.width)-width)/2;
    if(x<std::numeric_limits<std::int32_t>::min() ||
       x>std::numeric_limits<std::int32_t>::max())
        return false;
    result=current;
    result.left=static_cast<std::int32_t>(x);
    result.width=static_cast<std::int32_t>(width);
    return true;
}
} // namespace r2fix
