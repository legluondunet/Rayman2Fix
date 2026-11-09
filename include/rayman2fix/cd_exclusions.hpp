#pragma once

#include <filesystem>
#include <string>

namespace r2fix {

// The three root-level ancillary copy-protection files confirmed for the
// user's original RAYMAN2 CD. These are not game assets required by the
// planned no-CD runtime. Never match arbitrary *.tmp, *.016 or *.256 files,
// or files under game-data subdirectories.
inline bool excluded_cd_protection_auxiliary(const std::filesystem::path& relative) {
    const std::string path = relative.generic_u8string();
    return path == "00000001.tmp" ||
           path == "0000040c.016" ||
           path == "0000040c.256";
}

} // namespace r2fix
