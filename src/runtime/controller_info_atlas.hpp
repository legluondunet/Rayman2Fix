#pragma once
#include "controller_info_art.hpp"
#ifdef RAYMAN2FIX_EMBEDDED_CONTROLLER_ART
#include <cstring>
namespace r2fix::controller_atlas {
inline bool paint(HDC target,PadLabelFamily family) noexcept {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&paint),&module))return false;
    const unsigned id=family==PadLabelFamily::xbox?2202u:
        family==PadLabelFamily::playstation?2203u:2201u;
    HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!resource)return false;
    const DWORD size=SizeofResource(module,resource);
    HGLOBAL loaded=LoadResource(module,resource);
    const void* bytes=loaded?LockResource(loaded):nullptr;
    if(!bytes||!size)return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!memory)return false;
    void* out=GlobalLock(memory);
    if(!out){GlobalFree(memory);return false;}
    std::memcpy(out,bytes,size);GlobalUnlock(memory);
    IStream* stream=nullptr;
    if(CreateStreamOnHGlobal(memory,TRUE,&stream)!=S_OK){GlobalFree(memory);return false;}
    struct Release{IStream* stream;~Release(){stream->Release();}} release{stream};
    using namespace Gdiplus;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return false;
    struct Shutdown{ULONG_PTR token;~Shutdown(){GdiplusShutdown(token);}} shutdown{token};
    Bitmap decoded(stream);
    if(decoded.GetLastStatus()!=Ok||decoded.GetWidth()!=kPadInfoWidth*3||
        decoded.GetHeight()!=kPadInfoHeight*3)return false;
    // Compose premultiplied alpha over the existing black/white GDI passes.
    const int w=kPadInfoWidth*3,h=kPadInfoHeight*3;
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(target);if(!dc)return false;
    void* pixels=nullptr;HBITMAP dib=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dib||!pixels){if(dib)DeleteObject(dib);DeleteDC(dc);return false;}
    auto old=SelectObject(dc,dib);bool ok=old&&old!=HGDI_ERROR;
    if(ok){
        Bitmap surface(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Graphics g(&surface);g.SetCompositingMode(CompositingModeSourceCopy);
        ok=g.Clear(Color(0,0,0,0))==Ok&&g.DrawImage(&decoded,0,0,w,h)==Ok;
        g.Flush(FlushIntentionSync);
    }
    if(ok){BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        ok=AlphaBlend(target,0,0,kPadInfoWidth,kPadInfoHeight,dc,0,0,w,h,blend)!=FALSE;}
    if(old&&old!=HGDI_ERROR)SelectObject(dc,old);
    DeleteObject(dib);DeleteDC(dc);return ok;
}
} // namespace r2fix::controller_atlas
#else
// Native cross-compilation without a Windows emulator can still use the
// same original source art, without the build-time rasterizer.
namespace r2fix::controller_atlas {
inline bool paint(HDC target,PadLabelFamily family) noexcept {
    return controller_art::paint(target,family);
}
}
#endif
