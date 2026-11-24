// Vulkan 1.1/1.2 ICD discovery via Wine's vulkan-1.dll, independent
// of Vulkan SDK headers and without running host commands.
// No rendering, device creation, persistent context or game modification.
// Public Vulkan ABI subset (Khronos); x86/Win32 calling conventions.
#include "rayman2fix/wine_vulkan_driver.hpp"
#include "rayman2fix/wine_vulkan_driver_policy.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace r2fix {
namespace {
using Handle=void*;
using Result=std::int32_t;
constexpr Result kSuccess=0;
constexpr std::uint32_t kInstanceInfo=0u;
constexpr std::uint32_t kInstanceCreateInfo=1u;
constexpr std::uint32_t kPhysicalDeviceProperties2=1000059001u;
constexpr std::uint32_t kDriverProperties=1000196000u;
constexpr std::uint32_t kApi11=(1u<<22)|(1u<<12);
constexpr unsigned kMaxPhysicalDevices=16u;
constexpr unsigned kMaxPropertyBytes=4096u;

struct ApplicationInfo {
    std::uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    std::uint32_t applicationVersion;
    const char* pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};
struct InstanceInfo {
    std::uint32_t sType;
    const void* pNext;
    std::uint32_t flags;
    const ApplicationInfo* pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
struct PhysicalProperties {
    // Khronos VkPhysicalDeviceProperties has apiVersion, driverVersion,
    // vendorID, deviceID, deviceType, deviceName[256] at its start;
    // later VkPhysicalDeviceLimits is intentionally opaque.
    alignas(8) unsigned char data[kMaxPropertyBytes]{};
};
struct PhysicalProperties2 {
    std::uint32_t sType;
    void* pNext;
    PhysicalProperties properties;
};
struct DriverProperties {
    std::uint32_t sType;
    void* pNext;
    std::uint32_t driverID;
    char driverName[256];
    char driverInfo[256];
    unsigned char conformanceVersion[4];
};
static_assert(offsetof(DriverProperties,driverName)==12u,
              "Vulkan x86 driver properties driverName ABI");
static_assert(offsetof(DriverProperties,driverInfo)==268u,
              "Vulkan x86 driver properties driverInfo ABI");
static_assert(offsetof(PhysicalProperties2,properties)==8u,
              "Vulkan x86 properties2 ABI");

using GetInstanceProcAddr=void* (WINAPI*)(Handle,const char*);
using CreateInstance=Result (WINAPI*)(const InstanceInfo*,const void*,Handle*);
using DestroyInstance=void (WINAPI*)(Handle,const void*);
using EnumeratePhysicalDevices=Result (WINAPI*)(Handle,std::uint32_t*,Handle*);
using GetPhysicalDeviceProperties=void (WINAPI*)(Handle,PhysicalProperties*);
using GetPhysicalDeviceProperties2=void (WINAPI*)(Handle,PhysicalProperties2*);
using EnumerateInstanceVersion=Result (WINAPI*)(std::uint32_t*);

struct Dll {
    HMODULE module=nullptr;
    ~Dll(){if(module)FreeLibrary(module);}
};
struct Instance {
    Handle value=nullptr;
    DestroyInstance destroy=nullptr;
    ~Instance(){if(value && destroy)destroy(value,nullptr);}
};
std::uint32_t load_u32(const unsigned char* data,unsigned at) noexcept {
    std::uint32_t result=0;
    std::memcpy(&result,data+at,sizeof(result));
    return result;
}
void diag(char* out,unsigned count,const char* text) noexcept {
    if(out && count>0u)std::snprintf(out,count,"%s",text);
}
bool has_terminated_string(const char* s,unsigned n) noexcept {
    return s && std::memchr(s,0,n)!=nullptr;
}
} // namespace

bool wine_vulkan_driver_for_display(
    const char* wine_display_name,const char* wine_device_id,
    char* result,unsigned capacity,
    char* diagnostic,unsigned diagnostic_capacity) noexcept {
    if(result && capacity)result[0]='\0';
    diag(diagnostic,diagnostic_capacity,"not-attempted");
    if(!wine_display_name || !wine_display_name[0] ||
       !result || capacity<32u) {
        diag(diagnostic,diagnostic_capacity,"invalid-primary-display");
        return false;
    }
    Dll dll;
    // Wine provides this Windows Vulkan loader as a bridge to Linux.
    // Never attempt to read arbitrary host files or execute commands.
    dll.module=LoadLibraryW(L"vulkan-1.dll");
    if(!dll.module){
        diag(diagnostic,diagnostic_capacity,"winevulkan-unavailable");
        return false;
    }
    const auto get=reinterpret_cast<GetInstanceProcAddr>(
        GetProcAddress(dll.module,"vkGetInstanceProcAddr"));
    if(!get){
        diag(diagnostic,diagnostic_capacity,"vulkan-loader-invalid");
        return false;
    }
    const auto create=reinterpret_cast<CreateInstance>(
        get(nullptr,"vkCreateInstance"));
    if(!create){
        diag(diagnostic,diagnostic_capacity,"create-instance-unavailable");
        return false;
    }
    ApplicationInfo app{kInstanceInfo,nullptr,
                        "Rayman2Fix Video Info",0u,
                        "Rayman2Fix",0u,kApi11};
    InstanceInfo info{kInstanceCreateInfo,nullptr,0u,
                      &app,0u,nullptr,0u,nullptr};
    Instance instance;
    Result created=create(&info,nullptr,&instance.value);
    if(created!=kSuccess || !instance.value) {
        // API 1.0 fallback is deliberately not used: the reliable
        // driverInfo string requires VkPhysicalDeviceProperties2.
        diag(diagnostic,diagnostic_capacity,"vulkan-instance-1.1-failed");
        return false;
    }
    instance.destroy=reinterpret_cast<DestroyInstance>(
        get(instance.value,"vkDestroyInstance"));
    if(!instance.destroy) {
        diag(diagnostic,diagnostic_capacity,"destroy-instance-missing");
        return false;
    }
    const auto enumerate=reinterpret_cast<EnumeratePhysicalDevices>(
        get(instance.value,"vkEnumeratePhysicalDevices"));
    const auto props=reinterpret_cast<GetPhysicalDeviceProperties>(
        get(instance.value,"vkGetPhysicalDeviceProperties"));
    const auto props2=reinterpret_cast<GetPhysicalDeviceProperties2>(
        get(instance.value,"vkGetPhysicalDeviceProperties2"));
    if(!enumerate || !props || !props2) {
        diag(diagnostic,diagnostic_capacity,"properties2-unavailable");
        return false;
    }
    std::uint32_t count=0;
    if(enumerate(instance.value,&count,nullptr)!=kSuccess ||
       count==0u || count>kMaxPhysicalDevices) {
        diag(diagnostic,diagnostic_capacity,"device-count-unavailable");
        return false;
    }
    Handle handles[kMaxPhysicalDevices]{};
    std::uint32_t found=count;
    if(enumerate(instance.value,&found,handles)!=kSuccess ||
       found!=count) {
        diag(diagnostic,diagnostic_capacity,"device-enumeration-unstable");
        return false;
    }
    unsigned matching=0;
    char candidate[128]{};
    for(std::uint32_t i=0;i<count;++i) {
        if(!handles[i])continue;
        PhysicalProperties physical{};
        props(handles[i],&physical);
        // Khronos layout: 0=apiVersion, 4=driverVersion,
        // 8=vendorID, 12=deviceID, 16=type, 20=deviceName[256].
        const auto vendor=load_u32(physical.data,8u);
        const auto device=load_u32(physical.data,12u);
        const char* name=reinterpret_cast<const char*>(physical.data+20u);
        if(!has_terminated_string(name,256u) ||
           !same_wine_vulkan_gpu(wine_display_name,wine_device_id,
                                 name,vendor,device))
            continue;
        PhysicalProperties2 physical2{};
        DriverProperties driver{};
        driver.sType=kDriverProperties;
        physical2.sType=kPhysicalDeviceProperties2;
        physical2.pNext=&driver;
        props2(handles[i],&physical2);
        if(!has_terminated_string(driver.driverName,
                                  sizeof(driver.driverName)) ||
           !has_terminated_string(driver.driverInfo,
                                  sizeof(driver.driverInfo)))
            continue;
        char formatted[128]{};
        if(!format_wine_vulkan_driver(
              driver.driverID,driver.driverName,driver.driverInfo,
              formatted,sizeof(formatted)))
            continue;
        ++matching;
        if(matching==1u)std::snprintf(
            candidate,sizeof(candidate),"%s",formatted);
    }
    if(matching!=1u) {
        diag(diagnostic,diagnostic_capacity,
             matching?"ambiguous-vulkan-gpu":"no-matching-vulkan-driver");
        return false;
    }
    if(std::strlen(candidate)+1u>capacity) {
        diag(diagnostic,diagnostic_capacity,"driver-info-too-long");
        return false;
    }
    std::snprintf(result,capacity,"%s",candidate);
    diag(diagnostic,diagnostic_capacity,
         "matched-wine-primary-vulkan-driver");
    return true;
}
} // namespace r2fix
