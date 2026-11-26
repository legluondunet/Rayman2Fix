#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/install_layout.hpp"
#include "rayman2fix/cd_exclusions.hpp"
#include "rayman2fix/disc.hpp"
#include "rayman2fix/extraction_console.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/exe_patch.hpp"
#include "rayman2fix/gameframe.hpp"
#include "rayman2fix/game_launch.hpp"
#include "rayman2fix/game_config.hpp"
#include "rayman2fix/gog_offline.hpp"
#include "rayman2fix/gog_hook_diagnostics.hpp"
#include "rayman2fix/startup.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
    const auto private_dir=layout::state_for_game(staged);
    if(fs::is_symlink(private_dir) ||
       (fs::exists(private_dir) && !fs::is_directory(private_dir)))
        throw std::runtime_error("unsafe installer receipt directory");
    fs::create_directories(private_dir);
    const auto receipt = private_dir / kReceipt;
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
        // SDL3 is launcher-owned, never an immutable CD receipt member.
        if (upper(name) == "SDL3.DLL")
            throw std::runtime_error("CD conflicts with reserved SDL3.dll");
        // Reject a retail file that conflicts with the launcher-owned fix.dll.
        if (name=="rayman2fix" || name.rfind("rayman2fix/",0)==0)
            throw std::runtime_error("CD conflicts with reserved rayman2fix/ directory");
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

bool bootstrap_fix_file(const std::string& name) {
    return name=="FIX.DLL" || name=="FIX.DLL.BAK" ||
           name=="GLIDE2X.DLL" || name=="GLIDE2X.DLL.BAK"
#ifdef RAYMAN2FIX_BUNDLED_SDL3
           || name=="SDL3.DLL"
#endif
           ;
}

// Fresh bootstrap game/ may contain only launcher-owned runtime DLLs
// and permitted backups, never unrelated game/user files.
void check_bootstrap_workspace(const fs::path& game_dir) {
    if(fs::exists(layout::state(game_dir.parent_path())) ||
       fs::is_symlink(layout::state(game_dir.parent_path())))
        throw std::runtime_error("state/ exists without a complete game; refusing overwrite");
    if(!fs::exists(game_dir))return;
    if(!fs::is_directory(game_dir) || fs::is_symlink(game_dir))
        throw std::runtime_error("game/ is not a safe directory");
    for(const auto& item:fs::directory_iterator(game_dir)) {
        const auto name=upper(item.path().filename().u8string());
        if(item.is_symlink() || !item.is_regular_file() ||
           !bootstrap_fix_file(name))
            throw std::runtime_error(
                "game/ contains unverified files; use an empty folder for a new install");
    }
}

bool verify_local_receipt(const fs::path& root, Startup& startup) {
    const auto receipt = layout::cd_receipt(root);
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

void copy_bootstrap_files(const fs::path& original, const fs::path& staged) {
    const auto source=original/"fix.dll";
    const auto dest=staged/"fix.dll";
    if(!exists_file(source) || fs::exists(dest) || fs::is_symlink(dest))
        throw std::runtime_error("cannot stage verified clean fix.dll");
    fs::copy_file(source,dest);
#ifdef _WIN32
    // Linux unit tests do not bundle a Windows Glide binary; the actual
    // installer runs as a Windows executable on both Windows and Wine.
    if (!gameframe_glide_ready(original))
        throw std::runtime_error("cannot stage unverified game-local glide2x.dll");
    const auto glide=staged/"glide2x.dll";
    if (fs::exists(glide) || fs::is_symlink(glide))
        throw std::runtime_error("CD contains a conflicting glide2x.dll");
    fs::copy_file(original/"glide2x.dll",glide);
    if (!gameframe_glide_ready(staged))
        throw std::runtime_error("staged game-local glide2x.dll verification failed");
#ifdef RAYMAN2FIX_BUNDLED_SDL3
    // Stage the exact pinned dependency, not any file supplied by the CD.
    constexpr auto sdl_sha = "6d7e653aa9c762ceda4fffc054a504dcce273e016eb9f1fe079b6409a058a9f7";
    const auto sdl_source = original / "SDL3.dll";
    const auto sdl_dest = staged / "SDL3.dll";
    if (!exists_file(sdl_source) || sha256_file(sdl_source) != sdl_sha ||
        fs::exists(sdl_dest) || fs::is_symlink(sdl_dest))
        throw std::runtime_error("cannot stage verified game-local SDL3.dll");
    fs::copy_file(sdl_source, sdl_dest);
    if (sha256_file(sdl_dest) != sdl_sha)
        throw std::runtime_error("staged game-local SDL3.dll verification failed");
#endif
    // Keep any previous user DLL preserved by the single .bak rotation.
    const auto old_backup=original/"glide2x.dll.bak";
    if (fs::exists(old_backup) || fs::is_symlink(old_backup)) {
        if (!exists_file(old_backup) ||
            fs::exists(staged/"glide2x.dll.bak") ||
            fs::is_symlink(staged/"glide2x.dll.bak"))
            throw std::runtime_error("unsafe GameFrame backup during game staging");
        fs::copy_file(old_backup,staged/"glide2x.dll.bak");
    }
#endif
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
// Publish game and its two independent receipts as one reversible operation.
// Existing clean bootstrap runtime is restored if either rename fails.
void publish_installation(const fs::path& game_dir, const fs::path& staging) {
    const auto root=game_dir.parent_path();
    const auto state=layout::state(root);
    const auto pending_state=root/"state.installing";
    if(fs::exists(state) || fs::is_symlink(state) ||
       !fs::is_directory(pending_state) || fs::is_symlink(pending_state) ||
       (!exists_file(pending_state/kReceipt) &&
        !exists_file(pending_state/gog::receipt_name)))
        throw std::runtime_error("unsafe pending/active installation receipt state");
    fs::path backup=game_dir; backup+=".bootstrap-backup";
    if(fs::exists(backup) || fs::is_symlink(backup))
        throw std::runtime_error("old bootstrap backup exists; refusing overwrite");
    const bool had_workspace=fs::exists(game_dir);
    if(had_workspace)fs::rename(game_dir,backup);
    bool installed=false;
    try {
        fs::rename(staging,game_dir);
        installed=true;
        fs::rename(pending_state,state);
    } catch(...) {
        std::error_code ignored;
        if(installed)fs::rename(game_dir,staging,ignored);
        if(had_workspace)fs::rename(backup,game_dir,ignored);
        throw;
    }
    if(had_workspace) {
        std::error_code ignored;
        fs::remove_all(backup,ignored);
    }
}

// The public bootstrap does not contain a proprietary game executable
// or the owner's private delta. Detect an exact SHA256-approved decrypted
// rayman2.exe beside Rayman2Fix.exe, for both fresh and existing installs.
std::optional<fs::path> find_local_unprotected_exe(const fs::path& launcher_root) {
    std::optional<fs::path> found;
    for (const auto& entry : fs::directory_iterator(launcher_root)) {
        if (upper(entry.path().filename().u8string()) != "RAYMAN2.EXE")
            continue;
        if (entry.is_symlink() || !entry.is_regular_file())
            throw std::runtime_error("unsafe local rayman2.exe beside installer");
        if (found)
            throw std::runtime_error("ambiguous local rayman2.exe filenames beside installer");
        found = entry.path();
    }
    return found;
}

// The original is preserved as rayman2.exe.bak and the receipt stays in
// state/. Unknown CD editions are never modified.
void patch_executable_if_supported(const fs::path& game_root, Startup& startup) {
    if (is_exe_replaced(game_root)) {
        startup.log("EXE-PATCH: installed patch marker present; original preserved as rayman2.exe.bak");
        return;
    }
    const auto supplied = find_local_unprotected_exe(startup.root());
    if (supplied) {
        const ExePatchHeader approved{
            kApprovedProtectedExeSize, kApprovedUnprotectedExeSize,
            kApprovedProtectedExeSha256, kApprovedUnprotectedExeSha256
        };
        // No fallback to another file or unverified EXE if the user
        // explicitly supplied a rayman2.exe with the wrong hash.
        if (!exists_file(*supplied) ||
            fs::file_size(*supplied) != approved.patched_size ||
            sha256_file(*supplied) != approved.patched_sha256)
            throw std::runtime_error(
                "local rayman2.exe does not match approved decrypted SHA256; original preserved");
        const auto original = game_root / "rayman2.exe";
        if (!exists_file(original) ||
            fs::file_size(original) != approved.original_size ||
            sha256_file(original) != approved.original_sha256) {
            startup.log("EXE-PATCH: local decrypted EXE valid, but protected source hash is unsupported; no change");
            return;
        }
        const auto applied = install_exe_replacement_from_file(
            game_root, *supplied, approved);
        startup.log("EXE-PATCH: approved local decrypted rayman2.exe installed "
                    "source_sha256=" + applied.original_sha256 +
                    " target_sha256=" + applied.patched_sha256 +
                    " original_backup=rayman2.exe.bak receipt=state/.rayman2fix-exe-patch.tsv");
        return;
    }
    const auto patch = available_exe_patch(startup.root());
    if (patch.empty()) {
        startup.log("EXE-PATCH: no approved local decrypted rayman2.exe, embedded patch or .r2patch sidecar; protected executable unchanged");
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
        std::cerr << "No installed Rayman 2 executable under game/.\n";
        return 2;
    }
    if (gog::has_receipt(root)) {
        std::string problem;
        if (!gog::verify_receipt(root, problem)) {
            startup.log("VERIFY-GOG: " + problem);
            std::cerr << "GOG installation verification failed: " << problem << '\n';
            return 2;
        }
        startup.log("VERIFY-GOG: local SHA256 receipt and approved executable verified");
        return 0;
    }
    if (!verify_local_receipt(root, startup)) {
        std::cerr << "Installation verification failed. Existing user files are unchanged.\n";
        return 2;
    }
    return 0;
}

int run_automatic(Startup& startup, bool install_only) {
    try {
        const fs::path root = startup.game_dir();
        if (has_game_executable(root)) {
            startup.log("AUTO: existing game found; verify before possible launch");
            if (verify_installation(startup) != 0) return 2;
            synchronize_rayman2_ubi_ini(root, startup);
            if (!gog::has_receipt(root)) patch_executable_if_supported(root, startup);
            if (verify_installation(startup) != 0) return 2;
            if (install_only) {
                startup.log("INSTALL-ONLY: existing installation checked, game not launched");
                std::cout << "Installation verified. Game not launched (--install-only).\n";
                return 0;
            }
            if (gog::has_receipt(root)) return launch_verified_game(startup);
            return report_runtime_status(root, startup);
        }

        check_bootstrap_workspace(root);
        const auto candidates = find_disc_candidates(startup.root());
        // GOG is inspected only in fresh-install flow. Do not execute a
        // setup EXE or use its filename/hash as game identity.
        if (gog::OfflineWorkspace::available()) {
            const auto adjacent = gog::candidates(startup.root(),
                Startup::executable_path(startup.root() / "Rayman2Fix.exe"));
            if (!adjacent.empty()) {
                auto workspace = std::make_unique<gog::OfflineWorkspace>(startup.root());
                const auto selected = gog::select(candidates, adjacent,
                    [&](const auto& path) { return workspace->probe(path); });
                if (selected.source == gog::Source::ambiguous ||
                    selected.source == gog::Source::unreadable) {
                    startup.log("GOG: ambiguous media or failed installer probe");
                    std::cerr << "Cannot select a unique supported installation source.\n";
                    return 2;
                }
                if (selected.source == gog::Source::gog) {
                    startup.log("GOG: recognized product 1207658940");
                    const auto staging = startup.root() / "game.installing";
                    const auto pending_state = startup.root() / "state.installing";
                    if (fs::exists(staging) || fs::is_symlink(staging) ||
                        fs::exists(pending_state) || fs::is_symlink(pending_state))
                        throw std::runtime_error("previous installation staging exists; no overwrite");
                    // Source archives are kept intact. No CD EXE replacement
                    // or CD audio initialization is applied to this edition.
                    std::cout << "Extracting and verifying original GOG archives...\n";
                    const auto payload = workspace->extract(selected.path,
                        [&](const auto& line) { startup.log(line); });
                    try {
                        gog::stage_payload(payload, staging);
                        for (const auto& line : gog::hook_diagnostics(staging / "rayman2.exe"))
                            startup.log(line);
                        synchronize_rayman2_ubi_ini(staging, startup);
                        gog::write_receipt(staging);
                        copy_bootstrap_files(root, staging);
                        std::string problem;
                        if (!gog::verify_receipt(staging, problem))
                            throw std::runtime_error("GOG staging verification failed: " + problem);
                        publish_installation(root, staging);
                    } catch (...) {
                        std::error_code ignored;
                        fs::remove_all(staging, ignored);
                        fs::remove_all(pending_state, ignored);
                        throw;
                    }
                    workspace.reset(); // Remove extracted payload and tool before gameplay.
                    if (verify_installation(startup) != 0) return 2;
                    startup.log("AUTO-INSTALL: GOG assets and SHA256 receipt published");
                    std::cout << "GOG installation verified. Original setup files preserved.\n";
                    if (install_only) return 0;
                    return launch_verified_game(startup);
                }
            }
        }
        if (candidates.empty()) {
            if (gog::OfflineWorkspace::available()) {
                startup.log("AUTO: no installed game, recognized GOG setup or ISO/CUE media");
                std::cerr << "No supported installation source. Place one original CD image "
                             "or one GOG offline setup with its companion BIN files beside Rayman2Fix.exe.\n";
                return 2;
            }
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

        // Stage the retail game and its receipts separately, then publish both.
        const fs::path staging = startup.root() / "game.installing";
        fs::path nested_staging = staging;
        nested_staging += ".staging";
        if (fs::exists(staging) || fs::exists(nested_staging) ||
            fs::exists(startup.root()/"state.installing"))
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
            fs::remove_all(startup.root()/"state.installing",ignored);
            throw;
        }
        startup.log("AUTO-INSTALL: CD files staged and committed to game/ "
                    "with snd_cpa.ini and state/ SHA256 receipt");
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
                  << (install_only
                      ? "\n  Game startup: skipped (--install-only)\n"
                      : "\n  Game startup: attempting supported EXE (first real-world test)\n");
        if (install_only) {
            startup.log("INSTALL-ONLY: new CD installation verified; game not launched");
            return 0;
        }
        return report_runtime_status(root, startup);
    } catch (const std::exception& error) {
        startup.log(std::string("AUTO-ERROR: ") + error.what());
        throw;
    }
}
} // namespace r2fix
