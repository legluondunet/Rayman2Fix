#pragma once
#include <filesystem>
#include <string>

namespace r2fix {
// On Windows, the official GameFrame x86 Glide 2.x DLL and Apache license
// are embedded into the launcher as RCDATA #1103 and #1104.
// Unknown or mismatched glide2x.dll files are backed up before replacement.
void ensure_gameframe_glide(const std::filesystem::path& game_dir);

// Cryptographic integrity check against the SHA-256 baked into the launcher.
// Returns false when this is a developer build with no bundled GameFrame.
bool gameframe_glide_ready(const std::filesystem::path& game_dir);
std::string gameframe_glide_expected_sha256();
}
