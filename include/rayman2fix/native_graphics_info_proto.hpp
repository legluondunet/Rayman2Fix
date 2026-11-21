#pragma once
namespace r2fix {
using GraphicsInfoLogger=void (*)(const char*);
void start_native_graphics_info_proto(GraphicsInfoLogger logger) noexcept;
}
