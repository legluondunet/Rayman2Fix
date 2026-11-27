#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace r2fix::gog {
namespace fs = std::filesystem;
inline constexpr std::string_view product_id = "1207658940";
inline constexpr std::string_view executable_sha256 =
    "82e56041bde6b135a29af70ec213d716ef9d3253d46fa2c893bd25a6cff7f891";
inline constexpr std::uint64_t executable_size = 1469440;

inline std::string ascii_lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Only the trusted extractor may inspect these candidates. Never run a
// candidate executable. No filename convention or setup hash is required.
inline std::vector<fs::path> candidates(const fs::path& root,
                                        const fs::path& launcher) {
    std::vector<fs::path> out;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_symlink() || !entry.is_regular_file() ||
            ascii_lower(entry.path().extension().u8string()) != ".exe")
            continue;
        if (fs::equivalent(entry.path(), launcher)) continue;
        out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

enum class Probe { unrelated, rayman2, unreadable };
enum class Source { missing, cd, gog, ambiguous, unreadable };
struct Selection {
    Source source = Source::missing;
    fs::path path;
};

// A failed probe is not permission to ignore a potentially valid source.
// The probing adapter must distinguish unsupported/unrelated files from
// operational errors; no archive fingerprint participates in this policy.
inline Selection select(const std::vector<fs::path>& discs,
                        const std::vector<fs::path>& setups,
                        const std::function<Probe(const fs::path&)>& probe) {
    std::vector<fs::path> matches;
    for (const auto& path : setups) {
        const auto result = probe(path);
        if (result == Probe::unreadable) return {Source::unreadable, path};
        if (result == Probe::rayman2) matches.push_back(path);
    }
    if (discs.size() + matches.size() > 1) return {Source::ambiguous, {}};
    if (!discs.empty()) return {Source::cd, discs.front()};
    if (!matches.empty()) return {Source::gog, matches.front()};
    return {};
}

// Strict product-ID output, not a substring match against a name or log.
inline bool matches_product(std::string_view output) {
    const auto first = output.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    const auto last = output.find_last_not_of(" \t\r\n");
    return output.substr(first, last - first + 1) == product_id;
}

inline bool matches_executable(std::uint64_t size, std::string_view sha256) {
    return size == executable_size && sha256 == executable_sha256;
}
} // namespace r2fix::gog
