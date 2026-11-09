#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/disc.hpp"
#include "rayman2fix/startup.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace r2fix {
namespace {
constexpr const char* kReceipt = ".rayman2fix-cd-files.tsv";
constexpr const char* kReceiptHeader = "RAYMAN2FIX-CD-FILES\t1";

std::string upper(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_path())
        return false;
    for (const auto& component : relative) {
        if (component == ".." || component == "." || component.empty())
            return false;
    }
    return true;
}
bool exists_file(const fs::path& file) {
    return fs::is_regular_file(file) && !fs::is_symlink(file);
}
bool has_game_executable(const fs::path& root) {
    return exists_file(root / "rayman2.exe");
}
bool contains_required_cd_files(const DiscInfo& info) {
    bool exe = false, audio = false, data = false;
    for (const auto& file : info.files) {
        if (file.skipped_by_cd_recipe || file.invalid_extent) continue;
        const auto name = file.relative_path.generic_u8string();
        if (name == "rayman2.exe") exe = true;
        if (name == "dll/snd_cpa_mmx.ini") audio = true;
        if (name.rfind("data/", 0) == 0) data = true;
    }
    return exe && audio && data;
}

std::vector<fs::path> find_disc_candidates(const fs::path& root) {
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.is_symlink()) continue;
        const auto ext = upper(entry.path().extension().u8string());
        if (ext == ".ISO" || ext == ".CUE")
            candidates.push_back(entry.path());
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

// Manifest generated from an observed CD image. It is a local file-size
// verification receipt, NOT an authenticated edition/hash profile.
void write_receipt(const fs::path& staged, const DiscInfo& info) {
    const auto receipt = staged / kReceipt;
    if (fs::exists(receipt))
        throw std::runtime_error("disc includes reserved Rayman2Fix receipt filename");
    std::ofstream out(receipt, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create CD installation receipt");
    out << kReceiptHeader << '\n';
    out << "VOLUME\t" << info.volume_id << '\n';
    std::size_t count = 0;
    for (const auto& file : info.files) {
        if (file.skipped_by_cd_recipe || file.invalid_extent) continue;
        const auto name = file.relative_path.generic_u8string();
        if (name.find('\t') != std::string::npos ||
            name.find('\n') != std::string::npos ||
            !safe_relative(file.relative_path))
            throw std::runtime_error("unsafe file name in CD receipt");
        out << "FILE\t" << file.size_bytes << '\t' << name << '\n';
        ++count;
    }
    if (!count || !out)
        throw std::runtime_error("CD installation receipt is incomplete");
}

// Do not silently replace files that belong to an existing user installation.
// The old artifact includes a Rayman2Fix/ directory with an inert runtime,
// so it is recognized as a bootstrap workspace rather than an installed game.
void check_bootstrap_workspace(const fs::path& game_dir) {
    if (!fs::exists(game_dir)) return;
    if (!fs::is_directory(game_dir) || fs::is_symlink(game_dir))
        throw std::runtime_error("Rayman2Fix path is not a safe directory");
    for (const auto& item : fs::directory_iterator(game_dir)) {
        if (item.is_symlink() || !item.is_regular_file())
            throw std::runtime_error("unrecognized content in existing Rayman2Fix directory; "
                                     "preserving user data (no overwrite)");
        const auto name = upper(item.path().filename().u8string());
        if (name != "RAYMAN2FIX.INI" && name != "RAYMAN2FIX.DLL")
            throw std::runtime_error("existing Rayman2Fix directory contains user files; "
                                     "automatic install refused to avoid overwriting them");
    }
}

bool verify_local_receipt(const fs::path& root, Startup& startup) {
    const auto receipt = root / kReceipt;
    std::ifstream in(receipt, std::ios::binary);
    if (!in) {
        startup.log("VERIFY: missing local CD receipt");
        std::cerr << "Cannot verify existing installation: CD receipt missing.\n";
        return false;
    }
    std::string line;
    if (!std::getline(in, line) || line != kReceiptHeader) {
        startup.log("VERIFY: unrecognized CD receipt format");
        return false;
    }
    if (!std::getline(in, line) || line != "VOLUME\tRAYMAN2") {
        startup.log("VERIFY: unsupported volume in receipt");
        return false;
    }
    std::size_t count = 0;
    bool exe_listed = false, audio_listed = false;
    while (std::getline(in, line)) {
        if (line.rfind("FILE\t", 0) != 0) {
            startup.log("VERIFY: unexpected CD receipt entry");
            return false;
        }
        const auto split = line.find('\t', 5);
        if (split == std::string::npos) return false;
        std::uint64_t expected = 0;
        try {
            const auto size_text = line.substr(5, split - 5);
            if (size_text.empty() || !std::all_of(size_text.begin(), size_text.end(),
                [](unsigned char c) { return c >= '0' && c <= '9'; }))
                return false;
            expected = std::stoull(size_text);
        } catch (...) { return false; }
        const fs::path relative = fs::u8path(line.substr(split + 1));
        if (!safe_relative(relative) || count >= 50000)
            return false;
        const fs::path file = root / relative;
        if (!exists_file(file) || fs::file_size(file) != expected) {
            startup.log("VERIFY: missing or size-mismatched game file=" +
                        relative.generic_u8string());
            std::cerr << "Game file missing or modified: " << relative.u8string() << '\n';
            return false;
        }
        exe_listed |= relative.generic_u8string() == "rayman2.exe";
        audio_listed |= relative.generic_u8string() == "dll/snd_cpa_mmx.ini";
        ++count;
    }
    // Audio initialization duplicates the source but does not modify it.
    if (!count || !exe_listed || !audio_listed ||
        !exists_file(root / "dll" / "snd_cpa.ini") ||
        fs::file_size(root / "dll" / "snd_cpa.ini") !=
            fs::file_size(root / "dll" / "snd_cpa_mmx.ini")) {
        startup.log("VERIFY: missing core executable/audio assets");
        return false;
    }
    startup.log("VERIFY: local CD receipt sizes OK files=" + std::to_string(count) +
                " (NOT cryptographic authentication of game edition)");
    return true;
}

void copy_bootstrap_files(const fs::path& original, const fs::path& staged) {
    for (const char* name : {"Rayman2Fix.ini", "Rayman2Fix.dll"}) {
        const auto src = original / name;
        if (!exists_file(src)) continue;
        const auto dest = staged / name;
        if (fs::exists(dest))
            throw std::runtime_error(std::string("CD content collides with runtime file: ") +
                                     name);
        fs::copy_file(src, dest);
    }
}

void initialize_audio(const fs::path& staged) {
    const auto src = staged / "dll" / "snd_cpa_mmx.ini";
    const auto dst = staged / "dll" / "snd_cpa.ini";
    if (!exists_file(src))
        throw std::runtime_error("CD is missing dll/snd_cpa_mmx.ini");
    // The original Lutris setup intentionally initializes this config from
    // the MMX variant. Any CD-provided dst is replaced in staging, never in
    // the user's existing installation.
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    fs::permissions(dst, fs::perms::owner_write, fs::perm_options::add);
}

// Staged installation is copied and checked before the existing bootstrap
// workspace is moved aside. If commit fails, restore the original directory.
void publish_installation(const fs::path& game_dir, const fs::path& staging) {
    const fs::path backup(game_dir.u8string() + ".bootstrap-backup");
    if (fs::exists(backup))
        throw std::runtime_error("previous bootstrap backup exists; refusing overwrite");
    const bool had_workspace = fs::exists(game_dir);
    if (had_workspace)
        fs::rename(game_dir, backup);
    try {
        fs::rename(staging, game_dir);
    } catch (...) {
        if (had_workspace) {
            std::error_code rollback_error;
            fs::rename(backup, game_dir, rollback_error);
        }
        throw;
    }
    if (had_workspace) {
        std::error_code ignored;
        fs::remove_all(backup, ignored);
    }
}

int report_runtime_status(const fs::path& root, Startup& startup) {
    // This release has only an inert Rayman2Fix.dll; neither a pinned,
    // validated retail game executable nor GameFrame/SDL3 is integrated.
    // Never launch an unpatched SafeDisc executable or claim it is playable.
    std::vector<std::string> missing;
    if (!exists_file(root / "Rayman2Fix.dll")) missing.emplace_back("Rayman2Fix.dll");
    if (!exists_file(root / "glide2x.dll")) missing.emplace_back("glide2x.dll (GameFrame)");
    if (!exists_file(root / "SDL3.dll")) missing.emplace_back("SDL3.dll");
    for (const auto& name : missing)
        startup.log("RUNTIME-MISSING: " + name);
    startup.log("LAUNCH-BLOCKED: no trusted retail executable hash profile; "
                "no game hooks/no-CD runtime, GameFrame or SDL3 integration yet");
    std::cerr << "CD files prepared/checked, but Rayman2Fix cannot launch the game yet: "
                 "no verified retail executable profile or functional no-CD/Glide runtime.\n";
    if (!missing.empty()) {
        std::cerr << "Missing runtime components:";
        for (const auto& name : missing) std::cerr << " " << name << ";";
        std::cerr << '\n';
    }
    return 2;
}
} // namespace

int verify_installation(Startup& startup) {
    const fs::path root = startup.game_dir();
    if (!has_game_executable(root)) {
        startup.log("VERIFY: no installed rayman2.exe");
        std::cerr << "No installed Rayman 2 executable under Rayman2Fix/.\n";
        return 2;
    }
    if (!verify_local_receipt(root, startup)) {
        std::cerr << "Installation verification failed. Existing user files are unchanged.\n";
        return 2;
    }
    return 0;
}

int run_automatic(Startup& startup) {
    try {
        const fs::path root = startup.game_dir();
        if (has_game_executable(root)) {
            startup.log("AUTO: existing game found; verify before possible launch");
            if (verify_installation(startup) != 0) return 2;
            return report_runtime_status(root, startup);
        }

        check_bootstrap_workspace(root);
        const auto candidates = find_disc_candidates(startup.root());
        if (candidates.empty()) {
            startup.log("AUTO: no installed game and no adjacent ISO/CUE media");
            std::cerr << "No installed game or CD image. Place one .iso or .cue beside "
                         "Rayman2Fix.exe, then run it without arguments.\n";
            return 2;
        }
        if (candidates.size() != 1) {
            startup.log("AUTO: ambiguous CD images count=" + std::to_string(candidates.size()));
            std::cerr << "Multiple .iso/.cue images beside launcher. Keep exactly one.\n";
            return 2;
        }

        startup.log("AUTO: found single adjacent disc image=" +
                    candidates.front().filename().u8string());
        const auto image = DiscImage::open(candidates.front());
        const auto& info = image.info();
        startup.log("AUTO-DISC: volume=" + info.volume_id +
                    " source=" + info.filename_source +
                    " files=" + std::to_string(info.files.size()) +
                    " omitted=" + std::to_string(info.skipped_files) +
                    " suspect=" + std::to_string(info.suspect_files) +
                    " extract_bytes=" + std::to_string(info.extract_bytes));
        if (info.volume_id != "RAYMAN2" || info.suspect_files ||
            !contains_required_cd_files(info)) {
            startup.log("AUTO: media unsupported or required CD files unavailable");
            std::cerr << "Unsupported/unsafe CD layout. Expected RAYMAN2 with "
                         "rayman2.exe, dll/snd_cpa_mmx.ini and game data, "
                         "and no unresolved suspect files.\n";
            return 2;
        }

        // The user asked to install directly into Rayman2Fix/, not leave
        // gameplay assets in DiscContent/. We stage as a sibling and then
        // atomically publish without overwriting arbitrary user data.
        const fs::path staging = startup.root() / "Rayman2Fix.installing";
        if (fs::exists(staging) || fs::exists(fs::path(staging.u8string() + ".staging")))
            throw std::runtime_error("previous installation staging exists; no overwrite");
        try {
            image.extract(staging);
            initialize_audio(staging);
            write_receipt(staging, info);
            copy_bootstrap_files(root, staging);
            if (!has_game_executable(staging))
                throw std::runtime_error("extracted game executable is missing");
            publish_installation(root, staging);
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(staging, ignored);
            throw;
        }
        startup.log("AUTO-INSTALL: CD files staged and committed to Rayman2Fix/ "
                    "with snd_cpa.ini and local size receipt; game not certified playable");
        std::cout << "Original CD files installed into Rayman2Fix/.\n";
        if (verify_installation(startup) != 0) return 2;
        return report_runtime_status(root, startup);
    } catch (const std::exception& error) {
        startup.log(std::string("AUTO-ERROR: ") + error.what());
        throw;
    }
}
} // namespace r2fix
