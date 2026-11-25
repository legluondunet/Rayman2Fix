#pragma once
// Offscreen GDI drawing only; no native game hooks or presentation side effects.
#include "rayman2fix/gamepad_control_info.hpp"
#include "rayman2fix/controller_info_layout.hpp"
#ifdef _WIN32
#include <windows.h>
namespace r2fix {
// Draw an opaque preview into a caller-owned memory DC. This does not
// install a game menu, alter input, or invoke Glide. Future compositor
// will provide the translucent native scene background.
bool draw_controller_info_preview(HDC dc,const PadInfoView& view) noexcept;
}
#endif
