#pragma once
namespace r2fix {
using MenuAiLogger=void (*)(const char*);
void start_menu_ai_controller_trace(MenuAiLogger logger) noexcept;
}
