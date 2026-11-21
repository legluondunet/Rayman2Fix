#pragma once
#include "rayman2fix/graphics_menu_controller_model.hpp"
namespace r2fix {
using MenuAiLogger=void (*)(const char*);
void start_menu_ai_controller_trace(MenuAiLogger logger) noexcept;
// Checked live controller snapshot, available only in the research runtime.
// Revalidates all script signatures and Mind/Intelligence pointers each time.
bool get_verified_menu_controller(MenuAiModel&, MenuAiState&) noexcept;
}
