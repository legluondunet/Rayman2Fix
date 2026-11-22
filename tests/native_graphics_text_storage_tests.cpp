#include <array>
#include <cstdlib>
#include <cstring>
#include "rayman2fix/native_graphics_text_storage.hpp"
using namespace r2fix;
static void check(bool ok) {if(!ok)std::abort();}

int main() {
    // Six original native localization slots 379..384 plus the separate
    // brightness slot 385. The test compares pointer IDENTITY, not just
    // copied contents: stale cached text broke the previous Wine build.
    constexpr unsigned kCount=6;
    const char* originals[kCount]={
        "RESOLUTION","SAVE RESOLUTION","LOW","MEDIUM","HIGH","OTHER"};
    const char* information[kCount]={
        " ","OUTPUT: 1920x1080 16:9 AUTO","GPU: AMD Radeon RX 6600M",
        "D3D9: atiumdag.dll","BPP: desktop 32 / Glide 16",
        "GLIDE: 1024x768x16"};
    const char* brightness="LUMINOSITY";
    std::array<const char*,kCount+1> table{};
    for(unsigned i=0;i<kCount;++i)table[i]=originals[i];
    table[kCount]=brightness;
    bool installed=false;
    const auto visit=[&](bool verified_video_page) {
        const auto action=native_graphics_text_transition(
            verified_video_page,installed);
        if(action==NativeGraphicsTextTransition::install_information_pointers){
            for(unsigned i=0;i<kCount;++i)table[i]=information[i];
            installed=true;
        } else if(action==
                  NativeGraphicsTextTransition::restore_original_pointers){
            for(unsigned i=0;i<kCount;++i)table[i]=originals[i];
            installed=false;
        }
        // Never touch the independent native brightness text.
        check(table[kCount]==brightness);
        for(unsigned i=0;i<kCount;++i) {
            const char* expected=installed?information[i]:originals[i];
            check(table[i]==expected);
            check(std::strcmp(table[i],expected)==0);
        }
        return action;
    };
    check(visit(false)==NativeGraphicsTextTransition::unchanged);
    check(visit(true)==
          NativeGraphicsTextTransition::install_information_pointers);
    check(visit(true)==NativeGraphicsTextTransition::unchanged);
    check(visit(false)==
          NativeGraphicsTextTransition::restore_original_pointers);
    check(visit(false)==NativeGraphicsTextTransition::unchanged);
    // Returning from Luminosity or another Options page must re-point all
    // six native handles, not merely copy bytes into an old cache.
    check(visit(true)==
          NativeGraphicsTextTransition::install_information_pointers);
    check(visit(false)==
          NativeGraphicsTextTransition::restore_original_pointers);
    return 0;
}
