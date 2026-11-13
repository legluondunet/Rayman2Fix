#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace r2fix {

struct DiscEntry {
    std::filesystem::path relative_path;
    std::uint32_t extent_lba = 0;
    std::uint32_t size_bytes = 0;
    bool skipped_by_cd_recipe = false; // Known legacy installer exclusion, not a verified DRM classification
    bool invalid_extent = false; // Inspect reports this, extraction refuses it
    std::string invalid_reason;
};

struct DiscInfo {
    std::string volume_id;
    std::string format;
    std::string filename_source; // Joliet UCS-2 long names, or ISO9660 primary fallback
    std::vector<DiscEntry> files;
    std::uint64_t total_bytes = 0; // All ISO directory declarations, including skipped entries
    std::uint64_t extract_bytes = 0; // Planned output size after exact legacy exclusions
    std::uint64_t skipped_bytes = 0;
    std::size_t skipped_files = 0;
    std::size_t suspect_files = 0; // Unrecognized metadata/extent anomalies; DO NOT extract
    std::uint64_t image_bytes = 0;         // Source ISO/BIN file size
    std::uint64_t volume_bytes = 0;        // ISO 9660 logical volume size
    std::uint64_t distinct_extent_bytes = 0; // Sum of distinct extractable extent/size pairs (may overlap)
    std::size_t shared_extent_references = 0;
};

// Actual completed extraction statistics. Calculated from successfully
// written files; never report planned bytes as completed work.
struct ExtractionResult {
    std::size_t copied_files = 0;
    std::uint64_t copied_bytes = 0;
    std::size_t skipped_files = 0;
    std::uint64_t skipped_bytes = 0;
};

struct ExtractionProgress {
    std::size_t copied_files = 0;
    std::size_t total_files = 0;
    std::uint64_t copied_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::filesystem::path current_file;
};
using ExtractionCallback = std::function<void(const ExtractionProgress&)>;

// Read-only parser for single-data-track ISO 9660 and MODE1 CUE/BIN images.
// Never execute any disc-provided installer or game binary.
class DiscImage {
public:
    static DiscImage open(const std::filesystem::path& source,
                          const std::filesystem::path& cue_data_override = {});
    const DiscInfo& info() const noexcept { return info_; }
    // Optional streaming progress callback (file completion and every 1 MiB
    // within larger files). Existing silent callers can omit the callback.
    ExtractionResult extract(
        const std::filesystem::path& destination,
        const ExtractionCallback& progress = {}) const;

private:
    std::filesystem::path data_file_;
    std::uint64_t image_bytes_ = 0;
    std::uint64_t track_start_ = 0;
    std::uint32_t sector_bytes_ = 2048;
    std::uint32_t user_data_offset_ = 0;
    bool joliet_ = false;
    std::set<std::filesystem::path> output_file_paths_;
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
