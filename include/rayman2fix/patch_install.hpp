#pragma once

#include "rayman2fix/exe_patch.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace r2fix {

// Replaces rayman2.exe only after the original and patch hashes have been
// checked. Renames the original to rayman2.exe.bak, never overwriting an
// existing backup. Stages and validates the target before committing.
ExePatchHeader install_exe_replacement(
    const std::filesystem::path& game_root,
    const std::vector<unsigned char>& patch);

// Verify the PATCH receipt and both executables against their own SHA-256.
// If a patch receipt is absent, reject an unexplained rayman2.exe.bak.
// If patched, expected_cd_hash/size must match the backup, not the new EXE.
// Returns false only on integrity violations and preserves existing files.
bool verify_exe_replacement(
    const std::filesystem::path& game_root,
    const std::string& expected_cd_sha256,
    std::uint64_t expected_cd_size,
    std::string& reason);

// Has the installer recorded an executable replacement?
bool is_exe_replaced(const std::filesystem::path& game_root);

} // namespace r2fix
