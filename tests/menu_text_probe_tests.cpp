#include "rayman2fix/menu_text_probe_model.hpp"
#include <stdexcept>
#include <iostream>
int main() {
    using namespace r2fix;
    constexpr int ids[11]{370,371,372,373,379,380,381,382,383,384,385};
    for(int i=0;i<11;++i)
        if(menu_text_handle_slot(ids[i])!=i)
            throw std::runtime_error("menu text handle mismatch");
    for(int h=379;h<=384;++h)
        if(!is_resolution_text_handle(h) || is_brightness_text_handle(h))
            throw std::runtime_error("resolution handle classified incorrectly");
    if(!is_brightness_text_handle(385) ||
       is_resolution_text_handle(385) ||
       menu_text_handle_slot(-1)!=-1 ||
       menu_text_handle_slot(378)!=-1 ||
       menu_text_handle_slot(386)!=-1 ||
       !is_ai_text_return_rva(0x829EEu) ||
       is_ai_text_return_rva(0x829E9u))
        throw std::runtime_error("scope or AI callsite mismatch");
    std::cout<<"Native menu text handle scope: PASS\n";
}
