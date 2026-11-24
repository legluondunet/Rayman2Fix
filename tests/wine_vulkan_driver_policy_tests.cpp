#include "rayman2fix/wine_vulkan_driver_policy.hpp"
#include <cstdlib>
#include <cstring>
using namespace r2fix;
static void ck(bool v){if(!v)std::abort();}
int main() {
    ck(hex4_at("PCI\\VEN_1002&DEV_73DF&SUBSYS_1234","VEN_")==0x1002u);
    ck(hex4_at("PCI\\VEN_1002&DEV_73DF&SUBSYS_1234","DEV_")==0x73dfu);
    ck(hex4_at("PCI\\VEN_BAD!&DEV_73DF","VEN_")==0u);
    char a[256]{},b[256]{};
    ck(normalize_gpu_name("AMD Radeon RX 6600M (RADV NAVI23)",a,256u)>10u);
    ck(normalize_gpu_name("AMD Radeon RX 6600M",b,256u)>10u);
    ck(std::strcmp(a,b)==0);
    ck(same_wine_vulkan_gpu(
        "AMD Radeon RX 6600M (RADV NAVI23)",
        "PCI\\VEN_1002&DEV_73DF","Unrelated GPU",0x1002u,0x73DFu));
    ck(!same_wine_vulkan_gpu(
        "AMD Radeon RX 6600M (RADV NAVI23)",
        "PCI\\VEN_1002&DEV_73DF",
        "AMD Radeon RX 6600M (RADV NAVI23)",0x1002u,0x163fu));
    ck(same_wine_vulkan_gpu(
        "AMD Radeon RX 6600M (RADV NAVI23)","",
        "AMD Radeon RX 6600M",0x1002u,0x73dfu));
    ck(!same_wine_vulkan_gpu(
        "AMD Radeon RX 6600M (RADV NAVI23)","",
        "AMD Radeon(TM) Graphics (RADV)",0x1002u,0x163fu));
    char displayed[128]{};
    ck(format_wine_vulkan_driver(3u,"radv",
        "Mesa 25.2.6 (git-cb3a83)",displayed,sizeof(displayed)));
    ck(std::strcmp(displayed,"RADV / Mesa 25.2.6")==0);
    ck(format_wine_vulkan_driver(3u,"radv",
        "Mesa 26.0.1-arch1.1",displayed,sizeof(displayed)));
    ck(std::strcmp(displayed,"RADV / Mesa 26.0.1-arch1.1")==0);
    ck(format_wine_vulkan_driver(0u,"NVIDIA",
        "590.48.01",displayed,sizeof(displayed)));
    ck(std::strcmp(displayed,"NVIDIA / 590.48.01")==0);
    ck(!format_wine_vulkan_driver(3u,"radv",
        "",displayed,sizeof(displayed)));
    ck(!format_wine_vulkan_driver(3u,"radv",
        "Mesa 26.0.1",displayed,8u));
    return 0;
}
