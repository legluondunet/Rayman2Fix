#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace r2fix {

// Exact root-only CD auxiliary exclusions requested by the owner for the
// original RAYMAN2 volume. loader.ini and dplayerx.dll are not required by
// the Rayman2Fix installation; directx/dplayx.dll is a different file in an
// excluded installer directory. Do not filter nested namesakes or extensions.
inline constexpr std::array<const char*, 10> kExcludedCdRootFiles = {{
    "00000001.tmp",
    "0000040c.016",
    "0000040c.256",
    "secdrv.sys",
    "rayman2.icd",
    "clcd32.dll",
    "clokspl.exe",
    "drvmgt.dll",
    "loader.ini",
    "dplayerx.dll",
}};

inline bool excluded_cd_root_auxiliary(const std::filesystem::path& relative) {
    const std::string path = relative.generic_u8string();
    for (const char* name : kExcludedCdRootFiles) {
        if (path == name) return true;
    }
    return false;
}

} // namespace r2fix
