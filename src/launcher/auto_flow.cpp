#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/cd_exclusions.hpp"
#include "rayman2fix/disc.hpp"
#include "rayman2fix/extraction_console.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/exe_patch.hpp"
#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/game_launch.hpp"
#include "rayman2fix/game_config.hpp"
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
constexpr const char* kReceiptHeader = "RAYMAN2FIX-CD-FILES\t2";
// The unprotected executable supplied by the project owner. No commercial
// EXE is bundled; only a verified byte-delta may eventually be added.
constexpr const char* kApprovedProtectedExeSha256 =
    "7927a106ac4f2244e304e57c196170f6cfb311af87ddf59eb6e6c48b9bb0d5e3";
constexpr std::uint64_t kApprovedProtectedExeSize = 676352;
constexpr const char* kApprovedUnprotectedExeSha256 =
    "9bae484bd84a4afad8d4e7b1bb30a85ce1ce2367f8451463696eabab28a72713";
constexpr std::uint64_t kApprovedUnprotectedExeSize = 1468928;

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
bool gameframe_bundle_file(const std::string& filename) {
    if (filename == "GLIDE2X.DLL" ||
        filename == "GAMEFRAME-LICENSE-APACHE.TXT" ||
        filename == "GLIDE2X.DLL.BAK" ||
        filename == "GAMEFRAME-LICENSE-APACHE.TXT.BAK") return true;
    for (const char* prefix : {"GLIDE2X.DLL.BAK.",
                                "GAMEFRAME-LICENSE-APACHE.TXT.BAK."}) {
        const std::string head(prefix);
        if (filename.rfind(head, 0) == 0 &&
            filename.size() > head.size() &&
            std::all_of(filename.begin() + static_cast<std::ptrdiff_t>(head.size()),
                        filename.end(), [](unsigned char c) {
                            return c >= '0' && c <= '9';
                        })) return true;
    }
    return false;
}
bool runtime_backup_name(const std::string& filename) {
    if (filename == "RAYMAN2FIX.DLL.BAK") return true;
    // Accept legacy launcher-generated numbered backups only so startup can
    // migrate them to one .bak before a CD install. The .bak.previous
    // transactional file is deliberately not accepted as a normal state.
    const std::string prefix = "RAYMAN2FIX.DLL.BACKUP.";
    if (filename.rfind(prefix, 0) != 0 || filename.size() == prefix.size())
        return false;
    return std::all_of(filename.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
                       filename.end(), [](unsigned char c) {
                           return c >= '0' && c <= '9';
                       });
}
bool has_game_executable(const fs::path& root) {
    return exists_file(root / "rayman2.exe");
}
bool contains_required_cd_files(const DiscInfo& info) {
    bool exe = false, audio = false, data = false;
    for (const auto& file : info.files) {
        if (file.skipped_by_cd_recipe || file.invalid_extent ||
            file.relative_path.generic_u8string() == "ubi.ini") continue;
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

// Manifest generated from the user's CD. Each installed file is SHA-256
// hashed for later integrity checks; this is NOT a trusted retail-edition
// manifest because the hash baseline is generated from the same unverified CD.
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
        // ubi.ini is a mutable game-facing config generated from
        // Rayman2Fix.ini; it is never part of the immutable CD receipt.
        if (file.skipped_by_cd_recipe || file.invalid_extent ||
            file.relative_path.generic_u8string() == "ubi.ini") continue;
        const auto name = file.relative_path.generic_u8string();
        if (name.find('\t') != std::string::npos ||
            name.find('\n') != std::string::npos ||
            !safe_relative(file.relative_path))
            throw std::runtime_error("unsafe file name in CD receipt");
        const fs::path installed = staged / file.relative_path;
        if (!exists_file(installed) || fs::file_size(installed) != file.size_bytes)
            throw std::runtime_error("CD file size mismatch while writing receipt");
        const auto digest = sha256_file(installed);
        out << "FILE\t" << file.size_bytes << '\t' << digest << '\t' << name << '\n';
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
        if (name != "RAYMAN2FIX.INI" && name != "RAYMAN2FIX.DLL" &&
            !runtime_backup_name(name) && !gameframe_bundle_file(name))
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
    std::string original_exe_hash;
    std::uint64_t original_exe_size = 0;
    while (std::getline(in, line)) {
        if (line.rfind("FILE\t", 0) != 0) {
            startup.log("VERIFY: unexpected CD receipt entry");
            return false;
        }
        const auto split = line.find('\t', 5);
        if (split == std::string::npos) return false;
        const auto hash_end = line.find('\t', split + 1);
        if (hash_end == std::string::npos) return false;
        const auto expected_hash = line.substr(split + 1, hash_end - split - 1);
        if (expected_hash.size() != 64 ||
            !std::all_of(expected_hash.begin(), expected_hash.end(),
                [](unsigned char c) {
                    return (c >= '0' && c <= '9') ||
                           (c >= 'a' && c <= 'f');
                }))
            return false;
        std::uint64_t expected = 0;
        try {
            const auto size_text = line.substr(5, split - 5);
            if (size_text.empty() || !std::all_of(size_text.begin(), size_text.end(),
                [](unsigned char c) { return c >= '0' && c <= '9'; }))
                return false;
            expected = std::stoull(size_text);
        } catch (...) { return false; }
        const fs::path relative = fs::u8path(line.substr(hash_end + 1));
        if (!safe_relative(relative) || count >= 50000)
            return false;
        const fs::path file = root / relative;
        // Some exact root-only excluded files were previously copied and
        // recorded by older Rayman2Fix builds. They are now omitted. Permit
        // their absence (e.g. already removed manually), but never relax
        // validation of any other file in the receipt.
        if (excluded_cd_root_auxiliary(relative) ||
            relative.generic_u8string() == "ubi.ini") {
            ++count;
            continue; // Game-facing ubi.ini is rebuilt from Rayman2Fix.ini.
        }
        if (relative.generic_u8string() == "rayman2.exe") {
            if (exe_listed) return false;
            exe_listed = true;
            original_exe_hash = expected_hash;
            original_exe_size = expected;
            ++count;
            continue; // The original has moved to .bak after a verified patch.
        }
        if (!exists_file(file) || fs::file_size(file) != expected) {
            startup.log("VERIFY: missing or size-mismatched game file=" +
                        relative.generic_u8string());
            std::cerr << "Game file missing or modified: " << relative.u8string() << '\n';
            return false;
        }
        if (sha256_file(file) != expected_hash) {
            startup.log("VERIFY: SHA256 mismatch file=" + relative.generic_u8string());
            std::cerr << "Game file SHA256 mismatch: " << relative.u8string() << '\n';
            return false;
        }
        audio_listed |= relative.generic_u8string() == "dll/snd_cpa_mmx.ini";
        ++count;
    }
    // A patched CD EXE is verified against the patch marker; the original
    // SHA256 in the CD receipt must then match rayman2.exe.bak.
    std::string exe_reason;
    if (!exe_listed || !verify_exe_replacement(
            root, original_exe_hash, original_exe_size, exe_reason)) {
        startup.log("VERIFY: original/patched executable check failed: " + exe_reason);
        return false;
    }
    // Audio initialization duplicates the source but does not modify it.
    if (!count || !audio_listed ||
        !exists_file(root / "dll" / "snd_cpa.ini") ||
        fs::file_size(root / "dll" / "snd_cpa.ini") !=
            fs::file_size(root / "dll" / "snd_cpa_mmx.ini")) {
        startup.log("VERIFY: missing core executable/audio assets");
        return false;
    }
    startup.log("VERIFY: local CD receipt size+SHA256 OK, original/backup+patched EXE OK files=" +
                std::to_string(count) +
                " (local integrity only; NOT authenticated edition hashes)");
    return true;
}

// Upgrade an already installed original CD without requiring a second
// extraction. Old Rayman2Fix receipts may list these six root-only files.
// Only an otherwise verified RAYMAN2 installation is eligible; the receipt
// is replaced with rollback before any original files are removed.
void remove_excluded_cd_auxiliaries(const fs::path& root, Startup& startup) {
    const fs::path receipt = root / kReceipt;
    std::ifstream input(receipt, std::ios::binary);
    if (!input) throw std::runtime_error("CD receipt missing during cleanup");
    std::string line;
    if (!std::getline(input, line) || line != kReceiptHeader ||
        !std::getline(input, line) || line != "VOLUME\tRAYMAN2")
        throw std::runtime_error("CD cleanup requires an authenticated receipt format");

    std::vector<std::string> retained;
    retained.emplace_back(kReceiptHeader);
    retained.emplace_back("VOLUME\tRAYMAN2");
    std::size_t removed_receipt_entries = 0;
    while (std::getline(input, line)) {
        const auto last_tab = line.rfind('\t');
        if (line.rfind("FILE\t", 0) != 0 || last_tab == std::string::npos)
            throw std::runtime_error("invalid CD receipt during cleanup");
        const auto path = fs::u8path(line.substr(last_tab + 1));
        if (!safe_relative(path))
            throw std::runtime_error("unsafe CD receipt path during cleanup");
        if (excluded_cd_root_auxiliary(path) ||
            path.generic_u8string() == "ubi.ini")
            ++removed_receipt_entries; // ubi.ini remains; only its hash is removed.
        else
            retained.push_back(line);
    }
    if (input.bad()) throw std::runtime_error("cannot read CD receipt during cleanup");
    // Windows does not allow renaming an open file. Close the receipt before
    // the transactional .previous/.new replacement below.
    input.close();

    const auto& names = kExcludedCdRootFiles;
    std::size_t present = 0;
    for (const auto* name : names) {
        const fs::path file = root / name;
        // Never follow links, remove directories, or touch similarly named
        // game assets in nested directories.
        if (fs::is_symlink(fs::symlink_status(file)))
            throw std::runtime_error("refusing to delete symlinked CD auxiliary");
        if (fs::exists(file)) {
            if (!exists_file(file))
                throw std::runtime_error("unexpected non-file CD auxiliary path");
            ++present;
        }
    }
    if (!removed_receipt_entries && !present) return;

    if (removed_receipt_entries) {
        fs::path pending = receipt;
        pending += ".new";
        fs::path backup = receipt;
        backup += ".previous";
        if (fs::exists(pending) || fs::exists(backup))
            throw std::runtime_error("CD receipt cleanup staging exists; refusing overwrite");
        {
            std::ofstream out(pending, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot stage cleaned CD receipt");
            for (const auto& entry : retained) out << entry << '\n';
            out.flush();
            if (!out) {
                out.close();
                fs::remove(pending);
                throw std::runtime_error("cannot write cleaned CD receipt");
            }
        }
        fs::rename(receipt, backup);
        try {
            fs::rename(pending, receipt);
        } catch (...) {
            std::error_code ignored;
            fs::rename(backup, receipt, ignored);
            fs::remove(pending, ignored);
            throw;
        }
        std::error_code ignored;
        fs::remove(backup, ignored);
        startup.log("CLEANUP: removed obsolete auxiliary entries from local SHA256 receipt=" +
                    std::to_string(removed_receipt_entries));
    }

    for (const auto* name : names) {
        const fs::path file = root / name;
        if (!fs::exists(file)) continue;
        std::error_code err;
        fs::permissions(file, fs::perms::owner_write, fs::perm_options::add, err);
        if (err) throw std::runtime_error("cannot make obsolete auxiliary writable");
        if (!fs::remove(file))
            throw std::runtime_error("cannot delete obsolete auxiliary file");
        startup.log(std::string("CLEANUP-OMIT: deleted obsolete CD auxiliary ") + name);
    }
    startup.log("CLEANUP: obsolete CD root files excluded; "
                "game data and user settings retained");
}

void copy_bootstrap_files(const fs::path& original, const fs::path& staged) {
    for (const auto& entry : fs::directory_iterator(original)) {
        const auto name = upper(entry.path().filename().u8string());
        if (name != "RAYMAN2FIX.INI" && name != "RAYMAN2FIX.DLL" &&
            !runtime_backup_name(name) && !gameframe_bundle_file(name))
            continue;
        const auto dest = staged / entry.path().filename();
        if (fs::exists(dest))
            throw std::runtime_error("CD content collides with runtime file: " +
                                     dest.filename().u8string());
        fs::copy_file(entry.path(), dest);
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
    fs::path backup = game_dir;
    backup += ".bootstrap-backup";
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

// Patch bytes are intentionally not bundled until we obtain the ORIGINAL
// retail executable and generate an edition-specific minimal delta.
// On a supported build, commit the new EXE with its original preserved under
// rayman2.exe.bak, never overwrite a previous backup.
void patch_executable_if_supported(const fs::path& game_root, Startup& startup) {
    if (is_exe_replaced(game_root)) {
        startup.log("EXE-PATCH: installed patch marker present; original preserved as rayman2.exe.bak");
        return;
    }
    const auto patch = available_exe_patch(startup.root());
    if (patch.empty()) {
        startup.log("EXE-PATCH: no embedded or local delta; original CD executable unchanged (no .bak yet)");
        return;
    }
    const auto header = inspect_exe_patch(patch);
    if (header.original_sha256 != kApprovedProtectedExeSha256 ||
        header.original_size != kApprovedProtectedExeSize ||
        header.patched_sha256 != kApprovedUnprotectedExeSha256 ||
        header.patched_size != kApprovedUnprotectedExeSize) {
        throw std::runtime_error("executable patch does not match approved original-to-unprotected SHA256 pair");
    }
    // If the CD image/edition is unrecognized, *do not* patch or rename the
    // user's original executable; keep it intact for later investigation.
    const auto exe = game_root / "rayman2.exe";
    if (!exists_file(exe) || fs::file_size(exe) != header.original_size ||
        sha256_file(exe) != header.original_sha256) {
        startup.log("EXE-PATCH: source hash not supported by bundled patch; original kept");
        return;
    }
    const auto applied = install_exe_replacement(game_root, patch);
    startup.log("EXE-PATCH: applied source_sha256=" + applied.original_sha256 +
                " target_sha256=" + applied.patched_sha256 +
                " backup=rayman2.exe.bak");
}

// Launch the one verified retail edition after the complete CD verification
// succeeded and the generated game configuration has been synchronized.
int report_runtime_status(const fs::path& root, Startup& startup) {
    if (is_exe_replaced(root)) {
        startup.log("EXE-PATCH: approved unprotected rayman2.exe and original .bak verified");
        std::cout << "Verified unprotected rayman2.exe; original retained in rayman2.exe.bak.\n";
    }
    // No SDL3 dependency is required for this first game launch; the game's
    // original keyboard input remains available for rendering tests.
    return launch_verified_game(startup);
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
    // One-time cleanup of files included by older installers. After this,
    // the receipt must still verify normally, without requiring the CD.
    remove_excluded_cd_auxiliaries(root, startup);
    if (!verify_local_receipt(root, startup)) {
        startup.log("VERIFY: installed game failed after auxiliary cleanup");
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
            synchronize_rayman2_ubi_ini(root, startup);
            patch_executable_if_supported(root, startup);
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
        ExtractionConsole console(std::cout);
        console.step(1, 8, "Inspecting original CD image...");
        const auto image = DiscImage::open(candidates.front());
        const auto& info = image.info();
        startup.log("AUTO-DISC: volume=" + info.volume_id +
                    " source=" + info.filename_source +
                    " files=" + std::to_string(info.files.size()) +
                    " omitted=" + std::to_string(info.skipped_files) +
                    " suspect=" + std::to_string(info.suspect_files) +
                    " extract_bytes=" + std::to_string(info.extract_bytes));
        console.plan(info);
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
        fs::path nested_staging = staging;
        nested_staging += ".staging";
        if (fs::exists(staging) || fs::exists(nested_staging))
            throw std::runtime_error("previous installation staging exists; no overwrite");
        ExtractionResult extraction{};
        try {
            console.step(2, 8, "Extracting selected CD files...");
            extraction = image.extract(staging,
                [&](const ExtractionProgress& progress) {
                    console.progress(progress);
                });
            console.step(3, 8, "Initializing audio settings...");
            initialize_audio(staging);
            console.step(4, 8, "Generating SHA-256 file receipt...");
            write_receipt(staging, info);
            console.step(5, 8, "Synchronizing Glide configuration from Rayman2Fix.ini...");
            synchronize_rayman2_ubi_ini(staging, startup);
            console.step(6, 8, "Checking executable patch compatibility...");
            patch_executable_if_supported(staging, startup);
            console.step(7, 8, "Publishing installed files...");
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
                    "with snd_cpa.ini and local SHA256 receipt; game startup test follows");
        console.step(8, 8, "Verifying installed files and executable hashes...");
        if (verify_installation(startup) != 0) return 2;
        console.summary(info, extraction, root);
        std::cout << "  SHA-256 receipt: verified\n"
                     "  Audio: snd_cpa.ini initialized\n"
                     "  Graphics: ubi.ini synchronized from Rayman2Fix.ini\n"
                  << "  Executable patch: "
                  << (is_exe_replaced(root) ?
                      "applied (original kept as rayman2.exe.bak)" :
                      "not applied (no supported embedded/local delta)") << '\n'
                  << "  GameFrame: "
                  << (gameframe_glide_ready(root) ? "verified" : "not verified")
                  << "\n  Game startup: attempting supported EXE (first real-world test)\n";
        return report_runtime_status(root, startup);
    } catch (const std::exception& error) {
        startup.log(std::string("AUTO-ERROR: ") + error.what());
        throw;
    }
}
} // namespace r2fix
