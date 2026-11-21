#pragma once
#include <cstdint>

// Verified 21-Nov-2026 against BOTH user-supplied game PE files.
// Original protected CD: 676352 bytes SHA256 7927a106...d5e3
//   SizeOfImage 0x117000, 3 sections.
// Approved deprotected runtime: 1468928 bytes SHA256 9bae484b...2713
//   SizeOfImage 0x1D7000, 6 sections.
// The launcher authenticates the *complete* patched EXE on disk. This
// fingerprint is only a secondary, early fail-closed in-memory check.
namespace r2fix {

constexpr std::uint32_t kOriginalCdGameImageSize=0x00117000u;
constexpr std::uint32_t kDeprotectedGameImageSize=0x001D7000u;
constexpr std::uint32_t kGameTimestamp=0x3807B68Bu;
constexpr std::uint32_t kGameEntryRva=0x0005FCC0u;
constexpr std::uint16_t kPe32I386=0x014Cu;
constexpr std::uint16_t kPe32OptionalMagic=0x010Bu;

struct GraphicsMenuPeFingerprint {
    std::uint16_t machine;
    std::uint16_t section_count;
    std::uint32_t timestamp;
    std::uint16_t optional_magic;
    std::uint32_t entry_rva;
    std::uint32_t size_of_image;
};

constexpr bool graphics_menu_accepts_deprotected_image(
    const GraphicsMenuPeFingerprint& pe) noexcept {
    return pe.machine==kPe32I386 &&
           pe.section_count==6 &&
           pe.timestamp==kGameTimestamp &&
           pe.optional_magic==kPe32OptionalMagic &&
           pe.entry_rva==kGameEntryRva &&
           pe.size_of_image==kDeprotectedGameImageSize;
}
} // namespace r2fix
