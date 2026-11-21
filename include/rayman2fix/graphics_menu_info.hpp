#pragma once

// Pure read-only values intended for Rayman 2's native Options > Resolution
// page. This model does not detect menus, draw text or change display modes.
// The native menu adapter may be added ONLY after the approved game binary's
// menu-state and video-mode write routes have been independently validated.
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>

namespace r2fix {

struct GraphicsMenuInfo {
    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;
    std::uint32_t internal_width = 1024;
    std::uint32_t internal_height = 768;
    std::uint32_t internal_depth_bits = 16;
    bool classic_4_3 = false;
    std::string renderer = "GameFrame / Glide2";

    // Only the Win32/Wine adapter and driver identity can be reported by a
    // game-side Win32 query. This is NOT an independently verified Linux host
    // Vulkan/OpenGL/Mesa device or native display driver version.
    std::string adapter_reported_by_win32 = "Indisponible";
    std::string driver_reported_by_win32 = "Indisponible";

    // The resolution of this game session's output surface, not desktop
    // preference, the 1024x768 internal Glide canvas, or a stale ubi.ini mode.
    std::string output_resolution() const {
        return std::to_string(output_width) + " x " +
               std::to_string(output_height);
    }
    std::string native_glide_mode() const {
        return std::to_string(internal_width) + " x " +
               std::to_string(internal_height) + " x " +
               std::to_string(internal_depth_bits);
    }
    std::string actual_aspect_ratio() const {
        if (!output_width || !output_height) return "Indisponible";
        // Preserve the familiar exact 16:10 label rather than reducing it
        // to 8:5. Never approximate arbitrary ultra-wide outputs as 21:9.
        if (std::uint64_t(output_width)*10u ==
            std::uint64_t(output_height)*16u) return "16:10";
        const auto divisor = std::gcd(output_width, output_height);
        return std::to_string(output_width / divisor) + ":" +
               std::to_string(output_height / divisor);
    }
    const char* aspect_policy() const noexcept {
        return classic_4_3 ? "4:3" : "Auto";
    }
};

inline GraphicsMenuInfo make_graphics_menu_info(
    std::uint32_t output_width, std::uint32_t output_height,
    bool classic_4_3) {
    if (output_width < 640 || output_width > 16384 ||
        output_height < 480 || output_height > 8640)
        throw std::invalid_argument("invalid Rayman2Fix display geometry");
    GraphicsMenuInfo info;
    info.output_width = output_width;
    info.output_height = output_height;
    info.classic_4_3 = classic_4_3;
    return info;
}

} // namespace r2fix
