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
// The read-only controller panel must not activate the old calibration.
// Apply only after an actual successful draw, with a live authenticated
// page and foreground owner. Preserve Escape and all unrelated keys.
inline unsigned suppress_controller_info_keys(bool owns_page,bool foreground,
    std::int32_t result,unsigned size,std::uint8_t* keys) noexcept {
    if(!owns_page || !foreground || result!=0 || size!=256 || !keys)return 0;
    constexpr unsigned blocked[]{0x1Cu,0x9Cu,0x39u,0xCBu,0xCDu,0xC8u,0xD0u};
    unsigned count=0;
    for(auto dik:blocked) {
        if(keys[dik]&0x80u)++count;
        keys[dik]&=0x7fu;
    }
    return count;
}
// Only the fingerprint-verified native Menu_Valider action (9). Do not
// neutralize Menu_Annuler (8), Menu_Entrer (7), axes or any gameplay action.
constexpr bool suppress_controller_info_ipt(bool owns_page,bool foreground,
    bool verified_entry,unsigned index,std::string_view name) noexcept {
    return owns_page && foreground && verified_entry && index==9 &&
        (name.empty() || name=="Menu_Valider" || name=="Action_Menu_Valider" ||
         name=="IPT_E_Entry_Action_Menu_Valider");
}
} // namespace r2fix
