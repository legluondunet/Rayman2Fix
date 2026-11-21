#include "rayman2fix/graphics_menu_controller_model.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
struct Fake {
    std::uintptr_t base=0x05100000u;
    std::vector<std::uint8_t> mem=std::vector<std::uint8_t>(0x6000);
    bool r32(std::uintptr_t addr,std::uint32_t& v)const {
        if(addr<base || addr-base>mem.size()-4)return false;
        std::memcpy(&v,mem.data()+addr-base,4);return true;
    }
    bool r8(std::uintptr_t addr,std::uint8_t& v)const {
        if(addr<base || addr-base>=mem.size())return false;
        v=mem.at(addr-base);return true;
    }
    void w32(std::uintptr_t a,std::uint32_t v) {
        if(a<base || a-base>mem.size()-4)throw std::runtime_error("fixture");
        std::memcpy(mem.data()+a-base,&v,4);
    }
    void w8(std::uintptr_t a,std::uint8_t v){mem.at(a-base)=v;}
};
void check(bool b,const char* what){if(!b)throw std::runtime_error(what);}
int main() {
    using namespace r2fix;
    Fake m;
    const auto base=m.base,model=base+0x100,nd=base+0x200,rd=base+0x240;
    const auto normal=base+0x400,reflex=base+0x600;
    const auto nl5=base+0x800,nl19=base+0x850,rl5=base+0x880;
    const auto main=base+0x1000,second=base+0x2100,brightness=base+0x2800;
    m.w32(model,nd);m.w32(model+4,rd);m.w32(model+16,1);
    m.w32(nd,normal);m.w32(nd+4,27);m.w32(rd,reflex);m.w32(rd+4,7);
    m.w32(normal+5*12,nl5);m.w8(normal+5*12+8,5);m.w32(nl5+12,main);
    for(unsigned off:{0x140u,0x190u,0x1e0u}) {
        m.w32(main+off,356);m.w8(main+off+7,4);
    }
    m.w32(main+0xd60,261);m.w8(main+0xd60+7,3);
    m.w32(normal+19*12,nl19);m.w8(normal+19*12+8,3);
    m.w32(nl19+8,second);m.w32(second+0x48,356);m.w8(second+0x4f,4);
    m.w32(reflex+5*12,rl5);m.w8(reflex+5*12+8,3);
    m.w32(rl5+8,brightness);m.w32(brightness+0x758,357);
    m.w8(brightness+0x75f,4);
    auto read32=[&](std::uintptr_t a,std::uint32_t&v){return m.r32(a,v);};
    auto read8=[&](std::uintptr_t a,std::uint8_t&v){return m.r8(a,v);};
    MenuAiModel cm{};
    check(inspect_menu_model(model,read32,read8,cm),"relocated model fingerprint");
    check(cm.valid(),"model metadata");
    const auto mind=model-24,ni=base+0x3400,ri=base+0x3440;
    m.w32(mind,model);m.w32(mind+4,ni);m.w32(mind+8,ri);
    m.w32(ni,model);m.w32(ri,model+4);
    m.w32(ni+8,normal+5*12);m.w32(ri+8,reflex+5*12);
    MenuAiState st{};
    check(inspect_menu_mind(cm,mind,read32,st) && st.normal==5 && st.reflex==5,
          "two independent active behavior indexes");
    m.w32(ni+8,normal+19*12);
    check(inspect_menu_mind(cm,mind,read32,st) && st.normal==19 && st.reflex==5,
          "normal behavior change");
    m.w32(ri+8,0);
    check(inspect_menu_mind(cm,mind,read32,st) && st.reflex==-1,
          "unknown reflex is not zero");
    m.w32(ri,model);
    check(!inspect_menu_mind(cm,mind,read32,st),"model mismatch rejected");
    m.w32(ri,model+4);
    m.w32(nd+4,26);
    check(!inspect_menu_model(model,read32,read8,cm),"wrong normal count rejected");
    m.w32(nd+4,27);m.w32(main+0x140,0x9999);
    check(!inspect_menu_model(model,read32,read8,cm),"wrong video procedure rejected");
    m.w32(main+0x140,356);m.w8(brightness+0x75f,3);
    check(!inspect_menu_model(model,read32,read8,cm),"brightness/reflex mixup rejected");
    m.w8(brightness+0x75f,4);
    check(!inspect_menu_model(0x0F000000,read32,read8,cm),"unmapped model rejected");
    check(menu_behavior_index(normal,27,normal+5*12)==5 &&
          menu_behavior_index(normal,27,normal+1)==-1 &&
          menu_behavior_index(reflex,7,reflex+5*12)==5,"index bounds");
    std::cout<<"Rayman2 menu AI model and live behavior synthetic tests: PASS\n";
}
