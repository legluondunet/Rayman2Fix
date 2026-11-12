#pragma once
#include "rayman2fix/fov_math.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstring>
#include <cstddef>
namespace r2fix {
// The game owns one dedicated camera for projected UI objects, including
// 3D glyphs. Read its identity from the validated, native global slot;
// do not guess UI by level name, font metrics or screen coordinates.
inline bool is_native_screen_camera(const void* candidate,
                                    const void* ui_camera) {
    return candidate!=nullptr && ui_camera!=nullptr &&
           candidate==ui_camera;
}
// Observation only: the last adjusted camera on this render thread
// may correlate with subsequent Glide sprite submissions. It is not
// proof that an individual sprite belongs to the camera's scene.
enum class RenderCameraRole : unsigned {
    Unknown=0, World=1, ScreenWorld=2
};
inline RenderCameraRole classify_render_camera(const void* candidate,
                                              const void* ui_camera) {
    if(!candidate || !ui_camera) return RenderCameraRole::Unknown;
    return candidate==ui_camera ? RenderCameraRole::ScreenWorld
                                : RenderCameraRole::World;
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
// Verified in owner's retail x86 EXE disassembly:
// 0x4206BC writes width at +0x0C; 0x4206BF writes height at +0x08.
// 0x422B13 uses horizontal origin +0x58; 0x422B26 uses Y +0x5C.
// Native width/height were INVERTED in the first viewport patch.
constexpr std::size_t kGLIViewportBytes=0x70;
constexpr std::size_t kGLIViewportHeightOffset=0x08;
constexpr std::size_t kGLIViewportWidthOffset=0x0C;
constexpr std::size_t kGLIViewportLeftOffset=0x58;
constexpr std::size_t kGLIViewportTopOffset=0x5C;
static_assert(kGLIViewportTopOffset+sizeof(std::int32_t)<=kGLIViewportBytes,
              "Invalid native GLI viewport offsets");

inline ScreenWorldViewport read_native_screen_viewport(const void* raw) {
    const auto* data=static_cast<const std::uint8_t*>(raw);
    ScreenWorldViewport rect{};
    std::memcpy(&rect.height,data+kGLIViewportHeightOffset,sizeof(rect.height));
    std::memcpy(&rect.width,data+kGLIViewportWidthOffset,sizeof(rect.width));
    std::memcpy(&rect.left,data+kGLIViewportLeftOffset,sizeof(rect.left));
    std::memcpy(&rect.top,data+kGLIViewportTopOffset,sizeof(rect.top));
    return rect;
}
inline void write_centered_native_viewport(void* raw,
                                          const ScreenWorldViewport& rect) {
    auto* data=static_cast<std::uint8_t*>(raw);
    std::memcpy(data+kGLIViewportWidthOffset,&rect.width,sizeof(rect.width));
    std::memcpy(data+kGLIViewportLeftOffset,&rect.left,sizeof(rect.left));
}
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
