#pragma once
namespace r2fix {
// Conservative, NON-authoritative native menu activity candidates.
// Never use these labels to perform a video write or block an engine API.
enum class MenuScopeCandidate { unknown, video_choices, brightness, other_menu };
constexpr MenuScopeCandidate classify_menu_scope(
    bool verified_model, bool verified_mind, bool menu_level,
    int normal, int reflex,
    bool v17_valid, int v17, bool v51_valid, int v51) noexcept {
    if(!verified_model || !verified_mind || !menu_level ||
       !v17_valid || !v51_valid || v51!=0)
        return MenuScopeCandidate::unknown;
    if(normal==5 && reflex==0 && v17>=1 && v17<=3)
        return MenuScopeCandidate::video_choices;
    if(normal==18 && reflex==5 && v17==5)
        return MenuScopeCandidate::brightness;
    if(normal==11 && reflex==0)
        return MenuScopeCandidate::other_menu;
    return MenuScopeCandidate::unknown;
}
constexpr const char* menu_scope_label(MenuScopeCandidate v) noexcept {
    switch(v) {
       case MenuScopeCandidate::video_choices: return "video-choice-candidate";
       case MenuScopeCandidate::brightness: return "brightness-candidate";
       case MenuScopeCandidate::other_menu: return "other-menu-candidate";
       default: return "unknown";
    }
}
}
