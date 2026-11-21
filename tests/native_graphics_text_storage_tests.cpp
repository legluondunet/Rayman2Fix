#include <array>
#include <cstdlib>
#include <cstring>
#include "rayman2fix/native_graphics_text_storage.hpp"
using r2fix::switch_stable_menu_labels;
static void check(bool result) {if(!result)std::abort();}
using Row=std::array<char,56>;
using Bank=std::array<Row,6>;
static void put(Row& row,const char* src) {
    std::memset(row.data(),0,row.size());
    std::strncpy(row.data(),src,row.size()-1);
}
int main() {
    Bank native{},info{},live{};
    const char* original[]={"RESOLUTION","SAVE RESOLUTION","LOW",
                            "MEDIUM","HIGH","OTHER"};
    const char* replacement[]={" ","OUTPUT: 1920x1080 16:9",
        "GPU: AMD Radeon RX 6600M","D3D9: atiumdag.dll",
        "BPP: desktop 32 / Glide 16","GLIDE: 1024x768x16"};
    for(unsigned i=0;i<6;++i){put(native[i],original[i]);put(info[i],replacement[i]);}
    // New MENU-level installation stages ORIGINAL localized labels
    // before anyone enters the video page. Stable pointers survive
    // subsequent switches to the information page and back.
    switch_stable_menu_labels(live,native);
    const char* fixed[6]{};
    for(unsigned i=0;i<6;++i) {
        fixed[i]=live[i].data();
        check(std::strcmp(live[i].data(),original[i])==0);
    }
    switch_stable_menu_labels(live,info);
    for(unsigned i=0;i<6;++i){
        check(live[i].data()==fixed[i]);
        check(std::strcmp(live[i].data(),replacement[i])==0);
    }
    // Brightness page uses original text while cached pointers persist.
    switch_stable_menu_labels(live,native);
    for(unsigned i=0;i<6;++i){
        check(live[i].data()==fixed[i]);
        check(std::strcmp(live[i].data(),original[i])==0);
    }
    switch_stable_menu_labels(live,info);
    for(unsigned i=0;i<6;++i) {
        check(live[i].data()==fixed[i]);
        check(std::strcmp(live[i].data(),replacement[i])==0);
    }
    switch_stable_menu_labels(live,native);
    check(std::strcmp(live[0].data(),"RESOLUTION")==0);
}
