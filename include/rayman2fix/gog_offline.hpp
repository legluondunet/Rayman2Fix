#pragma once
#include "rayman2fix/gog_extractor.hpp"
#include <optional>
namespace r2fix::gog {

// Owns an isolated temporary workspace next to the launcher. The binary
// is deployed only when adjacent EXE candidates need inspecting.
class OfflineWorkspace {
public:
    explicit OfflineWorkspace(const fs::path& root);
    ~OfflineWorkspace();
    OfflineWorkspace(const OfflineWorkspace&) = delete;
    OfflineWorkspace& operator=(const OfflineWorkspace&) = delete;
    Probe probe(const fs::path& candidate);
    // Extracts into an empty directory owned by this workspace.
    // The return path is valid only for the lifetime of the workspace.
    fs::path extract(const fs::path& setup);
    static bool available() noexcept;
private:
    fs::path workspace_, executable_;
    unsigned invocation_ = 0;
    ExtractorResult run(const Args& args, std::chrono::seconds timeout);
};

struct InventoryEntry {
    fs::path path;
    std::string normalized;
    FileRole classification;
    std::uintmax_t bytes;
};
std::vector<InventoryEntry> inventory(const fs::path& extracted);
void validate_payload(const fs::path& extracted,
                      const std::vector<InventoryEntry>& files);
} // namespace r2fix::gog
