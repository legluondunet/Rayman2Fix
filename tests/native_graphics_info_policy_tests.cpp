#include "rayman2fix/native_graphics_info_policy.hpp"
#include <stdexcept>
#include <iostream>
#include <initializer_list>
void require(bool v){if(!v)throw std::runtime_error("native graphics scope policy");}
int main(){
using namespace r2fix;
for(int i=379;i<=384;++i)require(graphics_info_text_handle(i));
require(graphics_info_brightness_preserved(385));
require(!graphics_info_text_handle(370) && !graphics_info_text_handle(385));
require(suppress_menu_video_call(true,true,true,true,NativeVideoCall::selection));
require(suppress_menu_video_call(true,true,true,true,NativeVideoCall::save));
require(!suppress_menu_video_call(true,true,true,true,NativeVideoCall::brightness));
// The validated MENU video-dispatch slots are protected immediately,
// even while the relocated controller is not yet found. After discovery
// exact script children provide the narrower same protection.
require(protected_menu_video_dispatch(true,true,false,true,NativeVideoCall::selection));
require(protected_menu_video_dispatch(true,true,false,true,NativeVideoCall::save));
require(protected_menu_video_dispatch(true,true,true,false,NativeVideoCall::selection));
require(protected_menu_video_dispatch(true,true,true,false,NativeVideoCall::save));
require(!protected_menu_video_dispatch(true,true,false,false,NativeVideoCall::selection));
require(!protected_menu_video_dispatch(true,false,false,true,NativeVideoCall::selection));
require(!protected_menu_video_dispatch(false,true,false,true,NativeVideoCall::selection));
require(!protected_menu_video_dispatch(true,true,true,true,NativeVideoCall::brightness));
for(unsigned broken=0;broken<4;++broken){
 require(!suppress_menu_video_call(broken!=0,broken!=1,broken!=2,broken!=3,
   NativeVideoCall::selection));
 require(!suppress_menu_video_call(broken!=0,broken!=1,broken!=2,broken!=3,
   NativeVideoCall::save));
}
// Regression: first video visit V51=0; Enter changes V51 to 1,
// and this persists across a later return to the video menu.
// Neither transition may silently reveal the interactive original UI.
for(unsigned latch: {0u,1u}) {
 for(int row=1;row<=4;++row)
   require(graphics_info_video_page(true,true,true,5,0,true,row,true,latch));
}
require(!graphics_info_video_page(true,true,true,5,0,true,1,true,2));
require(!graphics_info_video_page(true,true,true,5,0,true,0,true,1));
require(!graphics_info_video_page(true,true,true,5,0,true,5,true,1));
require(!graphics_info_video_page(true,true,true,18,5,true,5,true,1)); // brightness
require(!graphics_info_video_page(true,true,true,11,0,true,1,true,1)); // options
require(!graphics_info_video_page(true,true,true,5,1,true,1,true,1));
for(unsigned i=0;i<5;++i)
 require(!graphics_info_video_page(i!=0,i!=1,i!=2,5,0,
                                     i!=3,1,i!=4,1));
// Information is cached by the native MENU text widget. Retain it
// only during the exact brightness behavior after a verified VIDEO visit.
// This does not change V17 or the original brightness control.
require(graphics_info_brightness_with_panel(
    true,true,true,18,5,true,5,true,true));
for(int i=0;i<9;++i) {
 const bool ok=graphics_info_brightness_with_panel(
    i!=0,i!=1,i!=2,i==3?19:18,i==4?0:5,
    i!=5,i==6?4:5,i!=7,i!=8);
 require(!ok);
}
require(!graphics_info_brightness_with_panel(
    true,true,true,5,0,true,5,true,true)); // video normal
require(!graphics_info_brightness_with_panel(
    true,true,true,18,5,true,5,true,false)); // direct brightness
require(!graphics_info_brightness_with_panel(
    true,true,true,18,5,true,5,false,true)); // no prior text
std::cout<<"Native video commands limited to exact validated menu nodes: PASS\n";
}
