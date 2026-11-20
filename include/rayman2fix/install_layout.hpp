#pragma once
#include <filesystem>

namespace r2fix::layout {
namespace fs = std::filesystem;

// Clean installations only. No old-layout fallback or migration.
inline fs::path game(const fs::path& launcher_root) {
    return launcher_root / "game";
}
inline fs::path mods(const fs::path& launcher_root) {
    return launcher_root / "mods";
}
inline fs::path docs(const fs::path& launcher_root) {
    return launcher_root / "docs";
}
inline fs::path legacy_gameframe(const fs::path& launcher_root) {
    return launcher_root / "third_party" / "gameframe"; // old renderer location only
}
inline fs::path state(const fs::path& launcher_root) {
    return launcher_root / "state";
}
// Fresh CD installs stage the game and both receipts together before
// publishing. A staged executable must never claim the active receipt.
inline fs::path state_for_game(const fs::path& game_root) {
    const auto name = game_root.filename().u8string();
    return game_root.parent_path() /
        (name == "game.installing" ? "state.installing" : "state");
}
inline fs::path cd_receipt(const fs::path& game_root) {
    return state_for_game(game_root) / ".rayman2fix-cd-files.tsv";
}
inline fs::path exe_receipt(const fs::path& game_root) {
    return state_for_game(game_root) / ".rayman2fix-exe-patch.tsv";
}
} // namespace r2fix::layout
