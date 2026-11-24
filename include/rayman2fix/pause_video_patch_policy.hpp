#pragma once
// Approved shared Fix.sna 23/6 controller: exact original VIDEO scripts.
// Runtime uses these policies ONLY in a private copy, after matching the
// complete original script shape. No copyrighted data or source is shipped.
#include <cstdint>
#include "rayman2fix/native_video_disable_choices.hpp"
#include "rayman2fix/video_actor_motion_policy.hpp"
namespace r2fix {
struct PauseVideoScriptPlan {
    unsigned behavior,script,count;
    std::uint64_t shape;
};
constexpr PauseVideoScriptPlan kPauseVideoPlans[5]{
    {9u,1u,304u,0x3184f1d2bf00dd9dULL},
    {9u,2u,424u,0xb6a5f08ae8ef7914ULL},
    {9u,3u,33u,0x9902892c1e0da3c5ULL},
    {9u,4u,497u,0x3f613086ca03920bULL},
    {15u,2u,45u,0x9a1d153d13735006ULL}
};
inline bool rewrite_pause_video_script(
    NativeMenuScriptNode* copy,const PauseVideoScriptPlan& p) noexcept {
    if(!copy)return false;
    if(native_video_script_shape_hash(copy,p.count)!=p.shape ||
       copy[p.count-1u].type!=8u ||
       copy[p.count-1u].indent!=0u)return false;
    if(p.behavior==9u && p.script==1u) {
        // Keep the native V16/V17 initialization and ESC/Options
        // behavior, but remove the two verified actor position/face
        // SetAGO subtrees in THIS read-only VIDEO behavior only.
        // The old per-row motion otherwise moves the pause critter
        // behind the Glide information panel when arrows are pressed.
        if(copy[141].type!=2u || copy[141].parameter!=11u ||
           copy[142].type!=11u || copy[142].parameter!=17u ||
           copy[143].type!=12u || copy[143].parameter!=3u ||
           copy[144].type!=2u || copy[144].parameter!=11u ||
           copy[145].type!=11u || copy[145].parameter!=16u ||
           copy[146].type!=12u || copy[146].parameter!=0u)
            return false;
        return remove_video_only_actor_moves(copy,p.count,157u,p.shape);
    }
    if(p.behavior==9u && p.script==2u) {
        // Owns all 3 mutable VID_SelectResolution calls and save/labels.
        if(copy[0].type!=0u || copy[0].indent!=1u ||
           copy[1].type!=1u || copy[1].parameter!=45u ||
           copy[40].type!=4u || copy[40].parameter!=356u ||
           copy[50].type!=4u || copy[50].parameter!=356u ||
           copy[60].type!=4u || copy[60].parameter!=356u ||
           copy[71].type!=27u || copy[71].parameter!=379u ||
           copy[101].type!=27u || copy[101].parameter!=371u ||
           copy[422].type!=3u || copy[422].parameter!=261u)
            return false;
        copy[0]={0u,0u,0u,8u};
        return true;
    }
    if(p.behavior==9u && p.script==3u) {
        // Brightness page has been removed from the readonly VIDEO menu.
        if(copy[0].type!=0u || copy[0].indent!=1u ||
           copy[1].type!=1u || copy[1].parameter!=4u ||
           copy[2].type!=11u || copy[2].parameter!=16u ||
           copy[3].type!=12u || copy[3].parameter!=5u ||
           copy[29].type!=4u || copy[29].parameter!=71u)
            return false;
        copy[0]={0u,0u,0u,8u};
        return true;
    }
    if(p.behavior==9u && p.script==4u) {
        // Keep two source-position SetAGO calls to preserve original
        // animated menu actor, then terminate old resolution widgets.
        if(copy[0].type!=0u || copy[0].parameter!=3u ||
           copy[3].type!=4u || copy[3].parameter!=503u ||
           copy[15].type!=4u || copy[15].parameter!=503u ||
           copy[27].type!=2u || copy[27].parameter!=11u ||
           copy[34].type!=27u || copy[34].parameter!=379u)
            return false;
        copy[27]={0u,0u,0u,8u};
        return true;
    }
    if(p.behavior==15u && p.script==2u) {
        // Another original resolution procedure in this shared model.
        if(copy[0].type!=0u || copy[0].indent!=1u ||
           copy[1].type!=1u || copy[1].parameter!=45u ||
           copy[6].type!=4u || copy[6].parameter!=356u)
            return false;
        copy[0]={0u,0u,0u,8u};
        return true;
    }
    return false;
}
} // namespace r2fix
