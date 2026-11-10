#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace r2fix {

// Explicit root-only exclusions requested for the original RAYMAN2 CD.
// Exact root-only protection-era auxiliaries. The original CD inspection
// confirmed "directx/dplayx.dll" (DirectPlay), NOT the misspelled root-level
// "dplayerx.dll" or "dxplayerx.dll". Those are not exclusion rules.
// Do not filter nested namesakes or extensions. Applies only to RAYMAN2.
inline constexpr std::array<const char*, 8> kExcludedCdRootFiles = {{
    "00000001.tmp",
    "0000040c.016",
    "0000040c.256",
    "secdrv.sys",
    "rayman2.icd",
    "clcd32.dll",
    "clokspl.exe",
    "drvmgt.dll",
}};

inline bool excluded_cd_root_auxiliary(const std::filesystem::path& relative) {
    const std::string path = relative.generic_u8string();
    for (const char* name : kExcludedCdRootFiles) {
        if (path == name) return true;
    }
    return false;
}

} // namespace r2fix
