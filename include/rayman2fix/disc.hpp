#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace r2fix {

struct DiscEntry {
    std::filesystem::path relative_path;
    std::uint32_t extent_lba = 0;
    std::uint32_t size_bytes = 0;
};

struct DiscInfo {
    std::string volume_id;
    std::string format;
    std::vector<DiscEntry> files;
    std::uint64_t total_bytes = 0;
    std::uint64_t image_bytes = 0;         // Source ISO/BIN file size
    std::uint64_t volume_bytes = 0;        // ISO 9660 logical volume size
    std::uint64_t distinct_extent_bytes = 0; // Sum of distinct extent/size pairs (may overlap)
    std::size_t shared_extent_references = 0;
};

// Read-only parser for single-data-track ISO 9660 and MODE1 CUE/BIN images.
// Never execute any disc-provided installer or game binary.
class DiscImage {
public:
    static DiscImage open(const std::filesystem::path& source);
    const DiscInfo& info() const noexcept { return info_; }
    void extract(const std::filesystem::path& destination) const;

private:
    std::filesystem::path data_file_;
    std::uint64_t image_bytes_ = 0;
    std::uint64_t track_start_ = 0;
    std::uint32_t sector_bytes_ = 2048;
    std::uint32_t user_data_offset_ = 0;
    DiscInfo info_;

    void validate_extent(std::uint32_t lba, std::uint32_t bytes) const;
    std::vector<unsigned char> read_user(std::uint64_t lba,
                                         std::uint64_t offset,
                                         std::size_t length) const;
    void scan_directory(std::uint32_t extent,
                        std::uint32_t size,
                        const std::filesystem::path& parent,
                        unsigned depth,
                        std::vector<std::uint32_t>& ancestors);
};

}  // namespace r2fix
