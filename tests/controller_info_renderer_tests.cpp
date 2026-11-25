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
    if(old && old!=HGDI_ERROR)SelectObject(dc,old);
    DeleteObject(bitmap);DeleteDC(dc);
    if(ok)std::cout<<"20 localized controller diagram variants rendered without clipping\n";
    return ok?0:3;
}
#endif
