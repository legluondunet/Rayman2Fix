#pragma once
#include <cstdint>
#include <cstddef>
#include "rayman2fix/graphics_menu_controller_model.hpp"
namespace r2fix {
struct MenuSelectionVars {
    bool selection_valid=false, activate_valid=false;
    std::int32_t selection=-1;
    std::uintptr_t selection_slot=0;
    unsigned activate=0;
};
template<class R32,class R8>
MenuSelectionVars observe_menu_selection(std::uintptr_t mind,R32 r32,
                                         R8 r8) noexcept {
    MenuSelectionVars result{};
    // Data from the hash-locked PC Menu.sna, controller's DsgVar schema.
    // V17 = 32-bit scalar at +0x144; V51 = 8-bit scalar at +0x4A5.
    // Both are looked up through the LIVE Mind->DsgMem->decl reference,
    // not dereferenced from static 0x0792... scene virtual addresses.
    if(!menu_safe_ptr(mind,16u))return result;
    std::uint32_t memory=0,reference=0,declaration=0;
    std::uint32_t infos=0,values=0;
    std::uint8_t count=0;
    if(!r32(mind+12u,memory) || !menu_safe_ptr(memory,12u) ||
       !r32(memory,reference) || !menu_safe_ptr(reference,4u) ||
       !r32(reference,declaration) || !menu_safe_ptr(declaration,16u) ||
       !r32(declaration+4u,infos) || !menu_safe_ptr(infos,90u*16u) ||
       !r8(declaration+12u,count) || count!=90u ||
       !r32(memory+8u,values) || !menu_safe_ptr(values,0x4A6u))
        return result;
    std::uint32_t offset=0,kind=0,value=0;
    if(r32(infos+17u*16u,offset) && offset==0x144u &&
       r32(infos+17u*16u+4u,kind) && kind==5u &&
       r32(std::uintptr_t(values)+offset,value)) {
        result.selection_valid=true;
        result.selection=static_cast<std::int32_t>(value);
        result.selection_slot=std::uintptr_t(values)+offset;
    }
    std::uint8_t flag=0;
    if(r32(infos+51u*16u,offset) && offset==0x4A5u &&
       r32(infos+51u*16u+4u,kind) && kind==0u &&
       r8(std::uintptr_t(values)+offset,flag)) {
        result.activate_valid=true;
        result.activate=flag;
    }
    return result;
}
}
