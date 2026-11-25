// Build-time rasterization of original controller illustrations.
// No game assets, image downloads or runtime file dependencies.
#include "../../src/runtime/controller_info_art.hpp"
#include <filesystem>
#include <iostream>
#include <vector>
#include <cwchar>
int main(int argc,char** argv) {
    using namespace Gdiplus;
    using namespace r2fix;
    if(argc!=2)return 1;
    ULONG_PTR token=0;GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 2;
    struct Shutdown{ULONG_PTR token;~Shutdown(){GdiplusShutdown(token);}} shutdown{token};
    UINT count=0,size=0;
    if(GetImageEncodersSize(&count,&size)!=Ok||!size)return 3;
    std::vector<BYTE> storage(size);
    auto* codecs=reinterpret_cast<ImageCodecInfo*>(storage.data());
    if(GetImageEncoders(count,size,codecs)!=Ok)return 4;
    CLSID png{};bool found=false;
    for(UINT i=0;i<count;++i)if(std::wcscmp(codecs[i].MimeType,L"image/png")==0){
        png=codecs[i].Clsid;found=true;break;
    }
    if(!found)return 5;
    const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
    constexpr int scale=3;
    for(auto family:{PadLabelFamily::generic,PadLabelFamily::xbox,PadLabelFamily::playstation}){
        Bitmap image(kPadInfoWidth*scale,kPadInfoHeight*scale,PixelFormat32bppARGB);
        {Graphics g(&image);if(g.Clear(Color(0,0,0,0))!=Ok||g.ScaleTransform(scale,scale)!=Ok)return 6;
         controller_art::draw(g,family);g.Flush(FlushIntentionSync);}
        auto path=dir/(family==PadLabelFamily::xbox?"xbox.png":
            family==PadLabelFamily::playstation?"playstation.png":"generic.png");
        if(image.Save(path.c_str(),&png,nullptr)!=Ok)return 7;
        std::cout<<path.string()<<" "<<image.GetWidth()<<"x"<<image.GetHeight()<<"\n";
    }
    return 0;
}
