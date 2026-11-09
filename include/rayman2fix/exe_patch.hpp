#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace r2fix {

struct ExePatchHeader {
    std::uint64_t original_size = 0;
    std::uint64_t patched_size = 0;
    std::string original_sha256;
    std::string patched_sha256;
};

// Apply a content-limited, SHA256-locked R2FXP01 delta to an owned retail
// executable. Never modify the original; destination must not exist.
// A real edition-specific delta is not bundled until both executable
// versions have been independently verified.
ExePatchHeader inspect_exe_patch(const std::vector<unsigned char>& patch);
ExePatchHeader apply_exe_patch(const std::filesystem::path& original,
                              const std::filesystem::path& destination,
                              const std::vector<unsigned char>& patch);

// On Windows, optionally obtain a delta embedded as RCDATA #1102.
// Empty until a verified hash-locked retail patch has been generated.
std::vector<unsigned char> bundled_exe_patch();

// For developer tests only: use a locally supplied, hash-locked
// Rayman2Fix-executable.r2patch beside Rayman2Fix.exe if no embedded
// patch is available. This sidecar is not part of release artifacts;
// the final build embeds the same verified bytes in the single EXE.
std::vector<unsigned char> available_exe_patch(
    const std::filesystem::path& launcher_root);

} // namespace r2fix
