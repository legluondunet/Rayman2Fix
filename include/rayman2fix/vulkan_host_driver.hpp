#pragma once
#include <cstddef>
namespace r2fix {
// Optional Wine Vulkan 1.2 driver information. Never calls host commands.
// Returns false on an ambiguous GPU match, missing Vulkan support, or
// absent driverInfo; Windows native continues to use its SetupAPI driver.
bool wine_host_vulkan_driver(const char* adapter_name,
                             const char* adapter_pci_id,
                             char* output,std::size_t capacity,
                             const char* (*log)(const char*)=nullptr) noexcept;
} // namespace r2fix
