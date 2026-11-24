#pragma once
namespace r2fix {
using VideoNavigationLogger=void (*)(const char*);
bool install_video_navigation_input(VideoNavigationLogger logger) noexcept;
} // namespace r2fix
