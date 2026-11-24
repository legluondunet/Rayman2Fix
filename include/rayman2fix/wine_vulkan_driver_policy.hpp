#pragma once
// No host processes, shell commands or environment assumptions.
// Wine's Vulkan bridge can reveal a Linux Vulkan ICD, but must be
// matched to the GPU that Wine reported as the primary display.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
namespace r2fix {
inline unsigned hex4_at(const char* text,const char* token) noexcept {
    if(!text || !token)return 0u;
    const auto* p=std::strstr(text,token);
    if(!p)return 0u;
    p+=std::strlen(token);
    unsigned value=0u;
    for(unsigned i=0;i<4;++i) {
        const unsigned char c=static_cast<unsigned char>(p[i]);
        if(!c)return 0u;
        value<<=4;
        if(c>='0' && c<='9')value+=c-'0';
        else if(c>='a' && c<='f')value+=10+c-'a';
        else if(c>='A' && c<='F')value+=10+c-'A';
        else return 0u;
    }
    return value;
}
inline unsigned normalize_gpu_name(const char* in,char* out,
                                   unsigned cap) noexcept {
    if(!in || !out || cap<2u)return 0u;
    unsigned n=0;
    for(const unsigned char* p=
        reinterpret_cast<const unsigned char*>(in);
        *p && n+1u<cap;++p) {
        // Only a trailing parenthesized Vulkan backend qualifier is
        // allowed to differ between Wine DISPLAY_DEVICE and Vulkan.
        if(*p=='(')break;
        if(std::isalnum(*p))out[n++]=char(std::tolower(*p));
    }
    out[n]='\0';
    return n;
}
inline bool same_wine_vulkan_gpu(
    const char* wine_primary_name,const char* wine_device_id,
    const char* vk_device_name,unsigned vk_vendor,unsigned vk_device) noexcept {
    // Strong identity always wins: vendor+device is the only acceptable
    // fallback when Wine's display name differs from the Vulkan name.
    const unsigned vendor=hex4_at(wine_device_id,"VEN_");
    const unsigned device=hex4_at(wine_device_id,"DEV_");
    if(vendor && device && vk_vendor && vk_device)
        return vendor==vk_vendor && device==vk_device;
    char a[256]{},b[256]{};
    const unsigned an=normalize_gpu_name(wine_primary_name,a,sizeof(a));
    const unsigned bn=normalize_gpu_name(vk_device_name,b,sizeof(b));
    return an>=10u && an==bn && std::strcmp(a,b)==0;
}
inline bool format_wine_vulkan_driver(
    unsigned driver_id,const char* driver_name,const char* driver_info,
    char* result,unsigned capacity) noexcept {
    if(!result || capacity<16u)return false;
    result[0]='\0';
    if(!driver_name || !driver_name[0] ||
       !driver_info || !driver_info[0])return false;
    // Identifying a Vulkan ICD is not a statement about the Glide
    // backend. The menu explicitly says "Vulkan" in the source log.
    const char* label=driver_id==3u?"RADV":driver_name;
    // Mesa's driverInfo often includes an extended distro/build
    // suffix. Keep the version without splitting its actual digits.
    const char* mesa=std::strstr(driver_info,"Mesa ");
    const char* info=mesa?mesa:driver_info;
    char short_info[64]{};
    unsigned n=0;
    for(const char* p=info;*p && n<sizeof(short_info)-1u;++p) {
        const unsigned char c=static_cast<unsigned char>(*p);
        if(c=='\r'||c=='\n'||c<32u)break;
        if(mesa && (c==' ' || c=='(') && n>=6u)break;
        short_info[n++]=*p;
    }
    if(!n)return false;
    const int count=std::snprintf(result,capacity,"%s / %s",label,short_info);
    return count>0 && unsigned(count)<capacity;
}
} // namespace r2fix
