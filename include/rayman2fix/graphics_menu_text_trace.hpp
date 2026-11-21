#pragma once
namespace r2fix {
using NativeMenuTextLogger=void(*)(const char*);
void start_native_menu_text_trace(NativeMenuTextLogger logger) noexcept;
}
