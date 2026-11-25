#pragma once
#include "rayman2fix/gamepad_input_policy.hpp"
#include <cstdint>

namespace r2fix {
// Only authenticated menu contexts. OR the high bit into the original
// native keyboard state; no global key events, no clearing physical keys.
inline unsigned merge_pad_menu_keys(PadContext context, bool foreground,
    std::int32_t result, unsigned size, std::uint8_t* keys,
    std::uint32_t actions) noexcept {
    if (!foreground || result != 0 || size != 256 || !keys ||
        (context != PadContext::menu && context != PadContext::pause_menu && context != PadContext::video &&
         context != PadContext::controller_info))
        return 0;
    unsigned count = 0;
    const auto set = [&](PadAction action, unsigned dik) {
        if (!(actions & pad_action_bit(action))) return;
        if (!(keys[dik] & 0x80u)) ++count;
        keys[dik] |= 0x80u;
    };
    // DIK_ESCAPE, DIK_RETURN and the four verified navigation arrows.
    set(PadAction::cancel, 0x01);
    if (context == PadContext::menu || context == PadContext::pause_menu) {
        if(context == PadContext::pause_menu) set(PadAction::pause, 0x01);
        set(PadAction::confirm, 0x1C);
        set(PadAction::left, 0xCB);
        set(PadAction::right, 0xCD);
        set(PadAction::up, 0xC8);
        set(PadAction::down, 0xD0);
    }
    return count;
}
} // namespace r2fix
