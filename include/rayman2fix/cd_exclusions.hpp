#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace r2fix {

// Explicit root-only exclusions requested for the original RAYMAN2 CD.
// The three numbered files were identified as old protection auxiliaries;
// the roles of the other three are not assumed here. Do not filter nested
// namesakes or extensions, and only apply this list to volume RAYMAN2.
inline constexpr std::array<const char*, 6> kExcludedCdRootFiles = {{
    "00000001.tmp",
    "0000040c.016",
    "0000040c.256",
    "secdrv.sys",
    "rayman2.icd",
    "dxplayerx.dll",
}};

inline bool excluded_cd_root_auxiliary(const std::filesystem::path& relative) {
    const std::string path = relative.generic_u8string();
    for (const char* name : kExcludedCdRootFiles) {
        if (path == name) return true;
    }
    return false;
}

} // namespace r2fix
