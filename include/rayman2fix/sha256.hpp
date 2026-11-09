#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace r2fix {

// Streaming SHA-256 for files in the installed-game integrity receipt.
// This verifies local file integrity, not authenticity of a retail edition.
class Sha256 {
public:
    Sha256();
    void update(const void* bytes, std::size_t count);
    std::string finish(); // Lowercase 64-character hexadecimal digest.
private:
    void process_block(const unsigned char* block);
    std::array<std::uint32_t, 8> state_{};
    std::array<unsigned char, 64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

std::string sha256_file(const std::filesystem::path& file);
std::string sha256_text(const std::string& value);

} // namespace r2fix
