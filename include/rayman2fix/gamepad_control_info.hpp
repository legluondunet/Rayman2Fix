#pragma once
// Presentation contract only: no SDL, Win32, rendering or native menu hooks.
#include "rayman2fix/gamepad_input_policy.hpp"
#include <array>
#include <string_view>

namespace r2fix {
enum class PadLabelFamily { generic, xbox, playstation };

struct PadControlRow {
    std::string_view action_key;
    std::string_view generic;
    std::string_view xbox;
    std::string_view playstation;
};

inline constexpr std::array<PadControlRow, 11> kPadControlRows{{
    {"movement", "Left stick", "Left stick", "Left stick"},
    {"jump", "South", "A", "Cross"},
    {"fire", "East", "B", "Circle"},
    {"camera_left", "West / left shoulder", "X / LB", "Square / L1"},
    {"camera_right", "North / right shoulder", "Y / RB", "Triangle / R1"},
    {"camera_rotation", "Right stick", "Right stick", "Right stick"},
    {"target", "Left trigger", "LT", "L2"},
    {"statistics", "Right trigger", "RT", "R2"},
    {"world_knowledge", "D-pad up", "D-pad up", "D-pad up"},
    {"murphy", "D-pad down", "D-pad down", "D-pad down"},
    {"pause", "Start", "Start", "Options"}
}};

constexpr std::string_view pad_control_label(const PadControlRow& row,
                                             PadLabelFamily family) noexcept {
    switch (family) {
        case PadLabelFamily::xbox: return row.xbox;
        case PadLabelFamily::playstation: return row.playstation;
        default: return row.generic;
    }
}

// The adapter supplies a confidently identified active family; never infer
// physical legends from an unverified device name. No controller means a
// generic reference layout, not a claim that a device is connected.
constexpr PadLabelFamily pad_display_family(bool connected,
                                            PadLabelFamily detected) noexcept {
    return connected ? detected : PadLabelFamily::generic;
}

constexpr std::string_view pad_back_label(PadLabelFamily family) noexcept {
    switch (family) {
        case PadLabelFamily::xbox: return "B";
        case PadLabelFamily::playstation: return "Circle";
        default: return "East";
    }
}
} // namespace r2fix
