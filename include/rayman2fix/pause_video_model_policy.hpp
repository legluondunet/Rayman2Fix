#pragma once
// Read-only, fail-closed identification of the shared Fix.sna graphics
// controller, distinct from the MENU 27/7 controller. No game bytes shipped.
#include <cstdint>
#include "rayman2fix/graphics_menu_controller_model.hpp"
namespace r2fix {
struct PauseVideoModel {
    std::uintptr_t model=0, mind=0;
    std::uintptr_t normal_entries=0, reflex_entries=0;
    int normal=-1, reflex=-1;
    bool valid() const noexcept {return model && mind && normal_entries && reflex_entries;}
};
template<class R32,class R8>
bool inspect_pause_video_model(std::uintptr_t address,
                               R32 read32,R8 read8,
                               PauseVideoModel& output) noexcept {
    output={};
    if(!menu_safe_ptr(address,24u) || address<0x10018u)return false;
    std::uint32_t nlist=0,rlist=0,flags=0,ne=0,re=0,n=0,r=0;
    if(!read32(address,nlist) || !read32(address+4u,rlist) ||
       !read32(address+16u,flags) || flags!=1u ||
       !menu_safe_ptr(nlist,8u) || !menu_safe_ptr(rlist,8u) ||
       !read32(nlist,ne) || !read32(nlist+4u,n) ||
       !read32(rlist,re) || !read32(rlist+4u,r) ||
       n!=23u || r!=6u ||
       !menu_safe_ptr(ne,23u*12u) ||
       !menu_safe_ptr(re,6u*12u))return false;
    // Original Fix.sna, shared (13,0): normal 9 script2 carries three
    // VID_SelectResolution calls, and the original VIDEO text handles.
    // Normal 15 script2 contains an independent fourth mode call.
    const auto script=[&](unsigned behavior,unsigned index,
                          unsigned count,std::uint32_t& ptr)->bool {
        std::uint32_t list=0;
        std::uint8_t actual=0;
        const auto entry=std::uintptr_t(ne)+behavior*12u;
        return read32(entry,list) && read8(entry+8u,actual) &&
               actual==count && menu_safe_ptr(list,count*4u) &&
               read32(std::uintptr_t(list)+index*4u,ptr) &&
               menu_safe_ptr(ptr,8u);
    };
    std::uint32_t v=0,additional=0;
    if(!script(9u,2u,5u,v) || !script(15u,2u,3u,additional))
        return false;
    const auto node=[&](std::uint32_t start,unsigned i,
                        std::uint32_t parameter,std::uint8_t kind)->bool {
        std::uint32_t p=0;
        std::uint8_t t=0;
        const auto addr=std::uintptr_t(start)+i*8u;
        return menu_safe_ptr(addr,8u) &&
               read32(addr,p) && read8(addr+7u,t) &&
               p==parameter && t==kind;
    };
    if(!node(v,40u,356u,4u) || !node(v,50u,356u,4u) ||
       !node(v,60u,356u,4u) || !node(v,71u,379u,27u) ||
       !node(v,101u,371u,27u) || !node(additional,6u,356u,4u))
        return false;
    // Validate Mind->AIModel and the reciprocal Intelligence pointers,
    // never infer a live controller from the static scene address alone.
    const auto mind=address-24u;
    std::uint32_t owner=0,ni=0,ri=0,ni_ref=0,ri_ref=0;
    std::uint32_t normal_current=0,reflex_current=0;
    if(!read32(mind,owner) || owner!=address ||
       !read32(mind+4u,ni) || !read32(mind+8u,ri) ||
       !menu_safe_ptr(ni,12u) || !menu_safe_ptr(ri,12u) ||
       !read32(ni,ni_ref) || ni_ref!=address ||
       !read32(ri,ri_ref) || ri_ref!=address+4u ||
       !read32(std::uintptr_t(ni)+8u,normal_current) ||
       !read32(std::uintptr_t(ri)+8u,reflex_current))
        return false;
    const int normal=menu_behavior_index(ne,n,normal_current);
    const int reflex=menu_behavior_index(re,r,reflex_current);
    if(normal<0 || reflex<0)return false;
    output={address,mind,ne,re,normal,reflex};
    return true;
}
} // namespace r2fix
