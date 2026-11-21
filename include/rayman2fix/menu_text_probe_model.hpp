#pragma once
namespace r2fix {
constexpr int menu_text_handle_slot(int h) noexcept {
    constexpr int ids[11]{370,371,372,373,379,380,381,382,383,384,385};
    for(int i=0;i<11;++i)if(ids[i]==h)return i;
    return -1;
}
constexpr bool is_resolution_text_handle(int h) noexcept {
    return h>=379 && h<=384;
}
constexpr bool is_brightness_text_handle(int h) noexcept {
    return h==385;
}
constexpr bool is_ai_text_return_rva(unsigned rva) noexcept {
    return rva==0x829EEu; // Game VA 0x4829EE after CALL at 0x4829E9
}
}
