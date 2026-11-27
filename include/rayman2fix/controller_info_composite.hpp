#pragma once
#include "rayman2fix/controller_info_layout.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>
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
    if(x<0 || y<0 || x>=kPadInfoWidth || y>=kPadInfoHeight)return {};
    // Straight edges and interior are exact whole-pixel coverage.
    // Only the four 12x12 corners require subpixel circle sampling.
    if((x>=12 && x<kPadInfoWidth-12) || (y>=12 && y<kPadInfoHeight-12))
        return x<2 || y<2 || x>=kPadInfoWidth-2 || y>=kPadInfoHeight-2 ?
            PadPanelCoverage{0,16}:PadPanelCoverage{16,0};
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
                                      const PadOverlayPixel* overlay,std::size_t overlay_count,
                                      unsigned width=kPadInfoWidth) noexcept {
    if(!dst||!overlay||!width||width>kPadInfoWidth||count!=std::size_t(width)*kPadInfoHeight||overlay_count!=kPadInfoPixels)return false;
    for(int y=0;y<kPadInfoHeight;++y)for(unsigned x=0;x<width;++x){
        const auto i=std::size_t(y)*width+x;
        // Box-average premultiplied coverage horizontally, preserving
        // thin lines and glyphs instead of nearest-neighbor truncation.
        const unsigned begin=x*kPadInfoWidth,end=(x+1u)*kPadInfoWidth;
        unsigned sum[8]{};
        for(unsigned sx=begin/width;sx<=(end-1u)/width;++sx){
            const unsigned weight=std::min(end,(sx+1u)*width)-std::max(begin,sx*width);
            const auto fg=overlay[std::size_t(y)*kPadInfoWidth+sx];
            const auto c=pad_panel_coverage(int(sx),y);
            const unsigned channels[]{fg.r,fg.g,fg.b,fg.ar,fg.ag,fg.ab,c.fill,c.border};
            for(unsigned j=0;j<8;++j)sum[j]+=channels[j]*weight;
        }
        for(auto& n:sum)n=(n+kPadInfoWidth/2u)/kPadInfoWidth;
        dst[i]=pad_composite565(dst[i],{std::uint8_t(sum[0]),std::uint8_t(sum[1]),std::uint8_t(sum[2]),
            std::uint8_t(sum[3]),std::uint8_t(sum[4]),std::uint8_t(sum[5])},{sum[6],std::min(sum[7],16u-sum[6])});
    }
    return true;
}
// Cache the exact box-average of the static overlay and panel coverage.
// The live scene and physical button lights must still be composed each frame.
enum class PadCompositeKind : std::uint8_t { preserve, constant, tint, partial };
struct PadPreparedPixel {
    PadOverlayPixel fg;
    std::uint8_t fill,border;
    PadCompositeKind kind=PadCompositeKind::partial;
    std::uint16_t constant=0;
};
constexpr PadPreparedPixel pad_prepare_pixel(PadOverlayPixel fg,PadPanelCoverage c) noexcept {
    PadPreparedPixel p{fg,std::uint8_t(c.fill),std::uint8_t(c.border)};
    if(!c.fill&&!c.border)p.kind=PadCompositeKind::preserve;
    else if((fg.ar==255u && fg.ag==255u && fg.ab==255u) || c.border==16u) {
        p.kind=PadCompositeKind::constant;
        p.constant=pad_composite565(0,fg,c);
    } else if(c.fill==16u && !fg.ar && !fg.ag && !fg.ab && !fg.r && !fg.g && !fg.b)
        p.kind=PadCompositeKind::tint;
    return p;
}
// An exact lookup for the ordinary translucent background, derived from
// the reference integer compositor, not an approximation or color change.
inline const std::array<std::uint16_t,65536>& pad_background_tint_table() noexcept {
    static const auto table=[] {
        std::array<std::uint16_t,65536> out{};
        for(unsigned i=0;i<out.size();++i)
            out[i]=pad_composite565(std::uint16_t(i),{},{16,0});
        return out;
    }();
    return table;
}
inline std::uint16_t pad_composite_prepared_pixel(std::uint16_t dst,const PadPreparedPixel& p,
    const std::array<std::uint16_t,65536>& tint) noexcept {
    switch(p.kind) {
    case PadCompositeKind::preserve:return dst;
    case PadCompositeKind::constant:return p.constant;
    case PadCompositeKind::tint:return tint[dst];
    default:return pad_composite565(dst,p.fg,{p.fill,p.border});
    }
}
inline bool prepare_controller_composite(const PadOverlayPixel* overlay,std::size_t overlay_count,
                                         unsigned width,std::vector<PadPreparedPixel>& output) noexcept {
    if(!overlay||!width||width>kPadInfoWidth||overlay_count!=kPadInfoPixels)return false;
    try {
        // Initialize the shared lookup during prewarm, not the first draw.
        (void)pad_background_tint_table();
        std::vector<PadPreparedPixel> staged(std::size_t(width)*kPadInfoHeight);
        for(int y=0;y<kPadInfoHeight;++y)for(unsigned x=0;x<width;++x){
            const unsigned begin=x*kPadInfoWidth,end=(x+1u)*kPadInfoWidth;
            unsigned sum[8]{};
            for(unsigned sx=begin/width;sx<=(end-1u)/width;++sx){
                const unsigned weight=std::min(end,(sx+1u)*width)-std::max(begin,sx*width);
                const auto fg=overlay[std::size_t(y)*kPadInfoWidth+sx];
                const auto c=pad_panel_coverage(int(sx),y);
                const unsigned channels[]{fg.r,fg.g,fg.b,fg.ar,fg.ag,fg.ab,c.fill,c.border};
                for(unsigned j=0;j<8;++j)sum[j]+=channels[j]*weight;
            }
            for(auto& n:sum)n=(n+kPadInfoWidth/2u)/kPadInfoWidth;
            staged[std::size_t(y)*width+x]=pad_prepare_pixel(
                {std::uint8_t(sum[0]),std::uint8_t(sum[1]),std::uint8_t(sum[2]),
                 std::uint8_t(sum[3]),std::uint8_t(sum[4]),std::uint8_t(sum[5])},
                {sum[6],std::min(sum[7],16u-sum[6])});
        }
        output.swap(staged);
        return true;
    }catch(...){return false;}
}
inline bool composite_prepared_controller(std::uint16_t* dst,std::size_t count,
                                          const PadPreparedPixel* prepared,std::size_t prepared_count,
                                          unsigned width) noexcept {
    if(!dst||!prepared||!width||width>kPadInfoWidth||
       count!=std::size_t(width)*kPadInfoHeight||prepared_count!=count)return false;
    const auto& tint=pad_background_tint_table();
    for(std::size_t i=0;i<count;++i)
        dst[i]=pad_composite_prepared_pixel(dst[i],prepared[i],tint);
    return true;
}
} // namespace r2fix
