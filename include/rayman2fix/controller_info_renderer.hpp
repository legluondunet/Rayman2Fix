#pragma once
// Offscreen GDI drawing only; no native game hooks or presentation side effects.
#include "rayman2fix/gamepad_control_info.hpp"
#include "rayman2fix/controller_info_layout.hpp"
#include "rayman2fix/controller_info_composite.hpp"
#include "rayman2fix/controller_info_activity.hpp"
#include <vector>
#ifdef _WIN32
#include <windows.h>
namespace r2fix {
// Draw an opaque preview into a caller-owned memory DC. This does not
// install a game menu, alter input, or invoke Glide. Future compositor
// will provide the translucent native scene background.
bool draw_controller_info_preview(HDC dc,const PadInfoView& view) noexcept;
// Build a private cached overlay. Output is replaced only on full success.
// Prepare only the private CPU overlay, never touch Glide or native menu state.
bool warm_controller_info_overlay(PadInfoLocale locale,bool connected,PadLabelFamily family) noexcept;
bool draw_controller_info_glide(PadInfoLocale locale,bool connected,PadLabelFamily family,float hud_scale,PadInfoActivity activity={}) noexcept;
bool prepare_controller_info_overlay(const PadInfoView& view,
                                     std::vector<PadOverlayPixel>& output) noexcept;
}
#endif
