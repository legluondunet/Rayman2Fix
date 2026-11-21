#pragma once
#include <cstdint>
#include <cstddef>
namespace r2fix {
struct MenuAiModel {
    std::uintptr_t model=0, normal_entries=0, reflex_entries=0;
    unsigned normal_count=0, reflex_count=0;
    bool valid() const noexcept {return model!=0 && normal_count==27 && reflex_count==7;}
};
struct MenuAiState {
    std::uintptr_t mind=0, normal_intelligence=0, reflex_intelligence=0;
    int normal=-1, reflex=-1;
    bool valid() const noexcept {return mind!=0;}
};
constexpr bool menu_safe_ptr(std::uintptr_t v,unsigned bytes=4) noexcept {
    return v>=0x10000u && bytes<=0x10000u &&
           v<=0xFFFFFFFFu-static_cast<std::uintptr_t>(bytes);
}
constexpr int menu_behavior_index(std::uintptr_t first,unsigned count,
                                   std::uintptr_t current) noexcept {
    if(!menu_safe_ptr(first,count*12u) || current<first ||
       current-first>=count*12u || (current-first)%12u!=0u) return -1;
    return static_cast<int>((current-first)/12u);
}
template<class R32,class R8>
bool inspect_menu_model(std::uintptr_t model,R32 r32,R8 r8,
                        MenuAiModel& out) noexcept {
    out={};
    if(!menu_safe_ptr(model,20))return false;
    std::uint32_t nd=0,rd=0,flags=0,nentries=0,rentries=0,n=0,r=0;
    if(!r32(model,nd) || !r32(model+4,rd) || !r32(model+16,flags) ||
       flags!=1u || !menu_safe_ptr(nd,8) || !menu_safe_ptr(rd,8) ||
       !r32(nd,nentries) || !r32(nd+4,n) ||
       !r32(rd,rentries) || !r32(rd+4,r) ||
       n!=27u || r!=7u || !menu_safe_ptr(nentries,27u*12u) ||
       !menu_safe_ptr(rentries,7u*12u))return false;
    const auto script=[&](std::uintptr_t entries,unsigned idx,
                          unsigned script_idx,unsigned expected,
                          std::uintptr_t& output)->bool {
        const auto behavior=entries+idx*12u;
        std::uint32_t list=0,script_ptr=0;
        std::uint8_t count=0;
        if(!r32(behavior,list) || !r8(behavior+8u,count) ||
           count!=expected || !menu_safe_ptr(list,count*4u) ||
           !r32(std::uintptr_t(list)+4u*script_idx,script_ptr) ||
           !menu_safe_ptr(script_ptr,8u))return false;
        output=script_ptr;
        return true;
    };
    const auto node=[&](std::uintptr_t script_ptr,unsigned offset,
                        unsigned id,std::uint8_t type)->bool {
        std::uint32_t param=0;
        std::uint8_t kind=0;
        return menu_safe_ptr(script_ptr,offset+8u) &&
               r32(script_ptr+offset,param) &&
               r8(script_ptr+offset+7u,kind) &&
               param==id && kind==type;
    };
    std::uintptr_t n5=0,n19=0,r5=0;
    // Hash-verified Menu.sna: normal 5/3 = video modes/save;
    // normal 19/2 = video mode; reflex 5/2 = brightness.
    return script(nentries,5u,3u,5u,n5) &&
           node(n5,0x140u,356u,4u) &&
           node(n5,0x190u,356u,4u) &&
           node(n5,0x1E0u,356u,4u) &&
           node(n5,0xD60u,261u,3u) &&
           script(nentries,19u,2u,3u,n19) &&
           node(n19,0x48u,356u,4u) &&
           script(rentries,5u,2u,3u,r5) &&
           node(r5,0x758u,357u,4u) &&
           ((out={model,nentries,rentries,27u,7u}).valid());
}
template<class R32>
bool inspect_menu_mind(const MenuAiModel& model,std::uintptr_t mind,
                       R32 r32,MenuAiState& out) noexcept {
    out={};
    if(!model.valid() || !menu_safe_ptr(mind,24u))return false;
    std::uint32_t model_ref=0,ni=0,ri=0,ni_ref=0,ri_ref=0;
    std::uint32_t normal_cur=0,reflex_cur=0;
    if(!r32(mind,model_ref) || model_ref!=model.model ||
       !r32(mind+4u,ni) || !r32(mind+8u,ri) ||
       !menu_safe_ptr(ni,24u) || !menu_safe_ptr(ri,24u) ||
       !r32(ni,ni_ref) || ni_ref!=model.model ||
       !r32(ri,ri_ref) || ri_ref!=model.model+4u ||
       !r32(ni+8u,normal_cur) || !r32(ri+8u,reflex_cur))
        return false;
    out={mind,ni,ri,
         menu_behavior_index(model.normal_entries,27u,normal_cur),
         menu_behavior_index(model.reflex_entries,7u,reflex_cur)};
    return true;
}
} // namespace r2fix
