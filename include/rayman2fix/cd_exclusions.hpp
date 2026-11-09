#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace r2fix {

// Explicit root-only exclusions requested for the original RAYMAN2 CD.
// Keep the existing owner-approved exclusions and the exact SafeDisc 1
// auxiliaries. In particular dplayerx.dll and dxplayerx.dll are distinct
// filenames. Do not filter nested namesakes or extensions: apply these
// exclusions only to the root of a volume labelled exactly RAYMAN2.
inline constexpr std::array<const char*, 10> kExcludedCdRootFiles = {{
    "00000001.tmp",
    "0000040c.016",
    "0000040c.256",
    "secdrv.sys",
    "rayman2.icd",
    "dxplayerx.dll",
    "clcd32.dll",
    "clokspl.exe",
    "dplayerx.dll",
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
