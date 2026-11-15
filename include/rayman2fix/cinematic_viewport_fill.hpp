#pragma once
// Native GLI viewport generator input guard (source RVA 0x20660).
// Integer coefficients are thousandths of the 1024x768 internal canvas.
// Expose this as a pure policy so rendering and synthetic tests agree.
// Original margins are restored after the approved native function returns.
namespace r2fix {
struct CinematicViewportFill {
    int top;
    int bottom;
    bool change;
};
constexpr CinematicViewportFill calculate_cinematic_viewport_fill(
    int width, int height, int x0, int x1, int y0, int y1) {
    // Full-width viewport, moderate letterbox-like inset only.
    // Reject arbitrary cropped UI panels, fades and unknown sizes.
    const bool supported=width==1024 && height==768 &&
        x0==0 && x1==1000 && y0>=0 && y0<=200 &&
        y1>=800 && y1<=1000 && y0<y1 &&
        y1-y0>=650 && (y0>0 || y1<1000);
    return {supported ? 0:y0, supported ? 1000:y1, supported};
}
} // namespace r2fix
