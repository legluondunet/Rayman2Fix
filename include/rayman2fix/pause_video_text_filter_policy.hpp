#pragma once
// Source-verified, in-game Pause VIDEO only: original UI TextSuperImposed
// still renders the old OPTIONS/RESOLUTION labels behind the standalone
// blue panel. Hide *only* the two approved 3D UI text source routes in
// the body band. Keep title, original butterfly and scene geometry.
#include <cmath>
namespace r2fix {
inline bool pause_video_hide_obsolete_native_text(
    bool verified_page_drawn,bool approved_ui_viewport,
    unsigned native_source_route,float original_y) noexcept {
    return verified_page_drawn && approved_ui_viewport &&
           (native_source_route==2u || native_source_route==3u) &&
           std::isfinite(original_y) &&
           original_y>0.28f && original_y<0.92f;
}
}
