#pragma once
#include "rayman2fix/controller_info_layout.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace r2fix {
// BGRA DIB samples from two identical drawings over black and white.
// Recover independent per-channel coverage so antialiasing never retains
// the opaque preview's background color in the live game.
struct PadOverlayPixel {
    std::uint8_t r=0,g=0,b=0, ar=0,ag=0,ab=0;
};
constexpr unsigned pad_channel(unsigned pixel,unsigned shift) noexcept {
    return (pixel>>shift)&255u;
}
constexpr unsigned pad_coverage(unsigned black,unsigned white) noexcept {
    return 255u-(white>black?white-black:0u);
}
constexpr PadOverlayPixel pad_overlay_from_dib(std::uint32_t black,std::uint32_t white) noexcept {
    const auto r=pad_channel(black,16),g=pad_channel(black,8),b=pad_channel(black,0);
    const auto ar=pad_coverage(r,pad_channel(white,16));
    const auto ag=pad_coverage(g,pad_channel(white,8));
    const auto ab=pad_coverage(b,pad_channel(white,0));
    return {std::uint8_t(std::min(r,ar)),std::uint8_t(std::min(g,ag)),std::uint8_t(std::min(b,ab)),
            std::uint8_t(ar),std::uint8_t(ag),std::uint8_t(ab)};
}
constexpr unsigned pad_over(unsigned dst,unsigned premul,unsigned alpha) noexcept {
    return std::min(255u,premul+(dst*(255u-alpha)+127u)/255u);
}
constexpr unsigned pad_tint(unsigned dst,unsigned src,unsigned alpha) noexcept {
    return (dst*(255u-alpha)+src*alpha+127u)/255u;
}
constexpr bool pad_rounded_sample(int x,int y,int inset) noexcept {
    constexpr int unit=8;
    const int l=inset*unit,t=l,r=kPadInfoWidth*unit-l,b=kPadInfoHeight*unit-t;
    const int radius=(12-inset)*unit;
    if(x<l||x>=r||y<t||y>=b)return false;
    const int dx=x<l+radius?l+radius-x:x>r-radius?x-r+radius:0;
    const int dy=y<t+radius?t+radius-y:y>b-radius?y-b+radius:0;
    return dx*dx+dy*dy<=radius*radius;
}
struct PadPanelCoverage {unsigned fill,border;};
constexpr PadPanelCoverage pad_panel_coverage(int x,int y) noexcept {
    PadPanelCoverage c{};
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
        const int px=x*8+sx*2+1,py=y*8+sy*2+1;
        if(pad_rounded_sample(px,py,2))++c.fill;
        else if(pad_rounded_sample(px,py,0))++c.border;
    }
    return c;
}
// GameFrame's Glide LFB uses swapped R/B compared to conventional RGB565.
// Keep this explicit at the boundary; no channel swap in the GDI renderer.
constexpr std::uint16_t pad_composite565(std::uint16_t dst,PadOverlayPixel fg,
                                          PadPanelCoverage c) noexcept {
    if(!c.fill&&!c.border)return dst;
    unsigned b=((dst>>11)&31u)*255u/31u,g=((dst>>5)&63u)*255u/63u,
             r=(dst&31u)*255u/31u;
    const unsigned f=c.fill*102u,e=c.border*255u,a=f+e;
    if(a){
        const auto tint=[&](unsigned base,unsigned fill,unsigned border){
            return pad_tint(base,(f*fill+e*border+a/2u)/a,(a+8u)/16u);
        };
        r=tint(r,18,145);g=tint(g,38,218);b=tint(b,74,255);
    }
    r=pad_over(r,fg.r,fg.ar);g=pad_over(g,fg.g,fg.ag);b=pad_over(b,fg.b,fg.ab);
    return std::uint16_t(((b*31u+127u)/255u)<<11 | ((g*63u+127u)/255u)<<5 |
                         ((r*31u+127u)/255u));
}
constexpr std::size_t kPadInfoPixels=std::size_t(kPadInfoWidth)*kPadInfoHeight;
inline bool composite_controller_info(std::uint16_t* dst,std::size_t count,
                                      const PadOverlayPixel* overlay,std::size_t overlay_count) noexcept {
    if(!dst||!overlay||count!=kPadInfoPixels||overlay_count!=kPadInfoPixels)return false;
    for(int y=0;y<kPadInfoHeight;++y)for(int x=0;x<kPadInfoWidth;++x){
        const auto i=std::size_t(y)*kPadInfoWidth+x;
        dst[i]=pad_composite565(dst[i],overlay[i],pad_panel_coverage(x,y));
    }
    return true;
}
} // namespace r2fix
