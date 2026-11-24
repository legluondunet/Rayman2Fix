#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
namespace r2fix {
// Wine's primary Win32 adapter and the first enumerated Vulkan ICD
// are not necessarily the same GPU (hybrid graphics).
inline std::string normalized_gpu_name(std::string_view s) {
    std::string result;
    for(char ch:s) {
        if(ch=='(')break;
        const auto c=static_cast<unsigned char>(ch);
        if(std::isalnum(c))
            result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}
inline bool same_host_gpu(std::string_view windows_name,
                          std::string_view vk_name,
                          std::uint32_t win_vendor,
                          std::uint32_t win_device,
                          std::uint32_t vk_vendor,
                          std::uint32_t vk_device) {
    if(win_vendor && win_device)
        return win_vendor==vk_vendor && win_device==vk_device;
    const auto a=normalized_gpu_name(windows_name);
    const auto b=normalized_gpu_name(vk_name);
    return a.size()>=9u && b.size()>=9u &&
           (a==b || (a.size()>=13u && b.find(a)!=std::string::npos));
}
inline bool parse_pci_id(std::string_view id,
                         std::uint32_t& vendor,
                         std::uint32_t& device) noexcept {
    vendor=device=0;
    const auto read=[&](std::string_view marker,std::uint32_t& out) {
        for(std::size_t pos=0;pos+marker.size()+4u<=id.size();++pos){
            bool eq=true;
            for(std::size_t k=0;k<marker.size();++k)
                if(std::toupper(static_cast<unsigned char>(id[pos+k]))!=marker[k]){
                    eq=false;break;
                }
            if(!eq)continue;
            std::uint32_t n=0;
            for(unsigned k=0;k<4u;++k){
                const char c=id[pos+marker.size()+k];
                unsigned v=0;
                if(c>='0' && c<='9')v=unsigned(c-'0');
                else if(c>='a' && c<='f')v=unsigned(c-'a')+10u;
                else if(c>='A' && c<='F')v=unsigned(c-'A')+10u;
                else return false;
                n=(n<<4)|v;
            }
            out=n;return true;
        }
        return false;
    };
    return read("VEN_",vendor) && read("DEV_",device) &&
           vendor && device;
}
} // namespace r2fix
