#include "rayman2fix/controller_info_renderer.hpp"
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
int main(){
    using namespace r2fix;
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc)return 1;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=kPadInfoWidth;bmi.bmiHeader.biHeight=-kPadInfoHeight;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap || !bits){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return 2;}
    auto old=SelectObject(dc,bitmap);
    bool ok=old && old!=HGDI_ERROR;
    for(unsigned i=0;i<5 && ok;++i){
        for(auto family:{PadLabelFamily::generic,PadLabelFamily::xbox,PadLabelFamily::playstation}){
            const auto view=make_pad_info_view(static_cast<PadInfoLocale>(i),true,family);
            if(!draw_controller_info_preview(dc,view)){
                std::cerr<<"controller preview layout failed locale="<<i<<" family="<<unsigned(family)<<"\n";
                ok=false;break;
            }
            GdiFlush();
            if(GetPixel(dc,20,45)==GetPixel(dc,460,145)){ok=false;break;}
        }
        if(ok)ok=draw_controller_info_preview(dc,make_pad_info_view(static_cast<PadInfoLocale>(i),false,PadLabelFamily::xbox));
    }
    if(ok){
        std::vector<PadOverlayPixel> overlay;
        ok=prepare_controller_info_overlay(make_pad_info_view(PadInfoLocale::fr,true,PadLabelFamily::playstation),overlay);
        if(ok){
            std::vector<std::uint16_t> scene(kPadInfoPixels,0xffffu);
            const auto body=overlay[145u*kPadInfoWidth+460u];
            const auto upper=overlay[150u*kPadInfoWidth+450u],lower=overlay[285u*kPadInfoWidth+460u];
            if(upper.r==lower.r&&upper.g==lower.g&&upper.b==lower.b)ok=false;
            ok=ok && overlay.size()==kPadInfoPixels && body.ar==255u && body.ag==255u && body.ab==255u &&
                !overlay[45u*kPadInfoWidth+20u].ar &&
                composite_controller_info(scene.data(),scene.size(),overlay.data(),overlay.size()) &&
                scene[0]==0xffffu && scene[45u*kPadInfoWidth+20u]!=0xffffu;
        }
    }
    if(old && old!=HGDI_ERROR)SelectObject(dc,old);
    DeleteObject(bitmap);DeleteDC(dc);
    if(ok)std::cout<<"20 localized controller diagram variants rendered without clipping\n";
    return ok?0:3;
}
#endif
