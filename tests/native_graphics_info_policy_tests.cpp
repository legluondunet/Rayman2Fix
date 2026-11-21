#include "rayman2fix/native_graphics_info_policy.hpp"
#include <stdexcept>
#include <iostream>
void require(bool v){if(!v)throw std::runtime_error("native graphics scope policy");}
int main(){
using namespace r2fix;
for(int i=379;i<=384;++i)require(graphics_info_text_handle(i));
require(graphics_info_brightness_preserved(385));
require(!graphics_info_text_handle(370) && !graphics_info_text_handle(385));
require(suppress_menu_video_call(true,true,true,true,NativeVideoCall::selection));
require(suppress_menu_video_call(true,true,true,true,NativeVideoCall::save));
require(!suppress_menu_video_call(true,true,true,true,NativeVideoCall::brightness));
for(unsigned broken=0;broken<4;++broken){
 require(!suppress_menu_video_call(broken!=0,broken!=1,broken!=2,broken!=3,
   NativeVideoCall::selection));
 require(!suppress_menu_video_call(broken!=0,broken!=1,broken!=2,broken!=3,
   NativeVideoCall::save));
}
std::cout<<"Native video commands limited to exact validated menu nodes: PASS\n";
}
