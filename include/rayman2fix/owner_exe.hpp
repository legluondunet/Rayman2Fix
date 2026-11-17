#pragma once

#include "rayman2fix/sha256.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace r2fix {

// Owner-supplied *complete* unprotected game EXE adjacent to the launcher.
// All filenames are untrusted, including "Rayman2.exe": allow only the
// caller's exact independently approved byte length and SHA256. Keep this
// pure helper testable with synthetic data, never commit commercial EXEs.
inline std::optional<std::filesystem::path> find_owner_executable(
    const std::filesystem::path& launcher_root,
    std::uint64_t expected_size, const std::string& expected_sha256) {
    namespace fs = std::filesystem;
    if (!expected_size || expected_sha256.size()!=64u)
        throw std::invalid_argument("invalid owner-executable allowlist");
    std::optional<fs::path> selected;
    for (const auto& entry : fs::directory_iterator(launcher_root)) {
        if (entry.is_symlink() || !entry.is_regular_file() ||
            entry.file_size()!=expected_size)
            continue;
        std::string extension=entry.path().extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char c) {return char(std::toupper(c));});
        if (extension!=".EXE" ||
            sha256_file(entry.path())!=expected_sha256)
            continue;
        if (selected)
            throw std::runtime_error("multiple approved unprotected game "
                "executables beside Rayman2Fix.exe; keep only one");
        selected=entry.path();
    }
    return selected;
}

} // namespace r2fix
