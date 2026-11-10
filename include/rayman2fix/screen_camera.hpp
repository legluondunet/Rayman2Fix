#pragma once
#include "rayman2fix/fov_math.hpp"
#include <cmath>
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
} // namespace r2fix
