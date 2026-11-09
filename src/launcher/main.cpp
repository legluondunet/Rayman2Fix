#include "rayman2fix/disc.hpp"
#include "rayman2fix/startup.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage() {
    std::cout
        << "Rayman2Fix 0.1.0 (bootstrap)\n"
        << "Disc inspection/extraction only. No game release is supported yet.\n\n"
        << "Usage:\n"
        << "  Rayman2Fix --inspect <game.iso|game.cue>\n"
        << "  Rayman2Fix --extract <game.iso|game.cue> [destination]\n"
        << "  Rayman2Fix --init\n"
        << "  Rayman2Fix --version\n"
        << "  Rayman2Fix --help\n\n"
        << "Default extraction destination: <launcher>/Rayman2Fix/DiscContent\n"
        << "Options/log: <launcher>/Rayman2Fix/Rayman2Fix.ini and <launcher>/Rayman2Fix.log\n"
        << "Files are copied offline from a disc image you own. The game is NOT\n"
        << "installed, patched, made DRM-free or launched by this bootstrap.\n";
}

static int run(const std::string& command, const fs::path& disc,
               const fs::path& destination, r2fix::Startup& startup) {
    const auto image = r2fix::DiscImage::open(disc);
    const auto& info = image.info();
    startup.log("DISC: filename_source=" + info.filename_source +
                " output_case=lowercase merged_directory_aliases=" +
                std::to_string(info.merged_directory_aliases));
    startup.log("DISC: format=" + info.format + " volume=" + info.volume_id +
                " files=" + std::to_string(info.files.size()) +
                " declared_bytes=" + std::to_string(info.total_bytes) +
                " distinct_extent_bytes=" + std::to_string(info.distinct_extent_bytes) +
                " shared_extent_references=" + std::to_string(info.shared_extent_references) +
                " image_bytes=" + std::to_string(info.image_bytes) +
                " volume_bytes=" + std::to_string(info.volume_bytes) +
                " planned_extract_bytes=" + std::to_string(info.extract_bytes) +
                " lutris_cd_ignored_files=" + std::to_string(info.skipped_files) +
                " lutris_cd_ignored_bytes=" + std::to_string(info.skipped_bytes) +
                " suspect_files=" + std::to_string(info.suspect_files));
    for (const auto& file : info.files) {
        if (file.skipped_by_cd_recipe)
            startup.log("DISC-OMIT: legacy Rayman 2 CD installer path=" +
                        file.relative_path.generic_u8string() +
                        " declared_bytes=" + std::to_string(file.size_bytes) +
                        " (not copied; protection status unverified)");
        else if (file.invalid_extent)
            startup.log("DISC-SUSPECT: path=" + file.relative_path.generic_string() +
                        " lba=" + std::to_string(file.extent_lba) +
                        " declared_bytes=" + std::to_string(file.size_bytes) +
                        " reason=" + file.invalid_reason +
                        " (inspect only; extraction refused)");
    }
    std::cout << "Disc image: " << disc.string() << '\n'
              << "Format: " << info.format << '\n'
              << "Filename source: " << info.filename_source << '\n'
              << "Extraction filename case: lowercase\n"
              << "Merged case-variant directories: " << info.merged_directory_aliases << '\n'
              << "ISO volume: " << info.volume_id << '\n'
              << "Files: " << info.files.size() << '\n'
              << "Declared file bytes: " << info.total_bytes << '\n'
              << "Distinct extent bytes: " << info.distinct_extent_bytes << '\n'
              << "Shared extent references: " << info.shared_extent_references << '\n'
              << "ISO volume bytes: " << info.volume_bytes << '\n'
              << "Physical image bytes: " << info.image_bytes << '\n'
              << "Selected extract bytes: " << info.extract_bytes << '\n'
              << "Known CD recipe exclusions: " << info.skipped_files << " files ("
              << info.skipped_bytes << " declared bytes)\n"
              << "Unknown suspect files: " << info.suspect_files
              << " (extraction blocked if nonzero)\n"
              << "Edition: UNKNOWN (no trusted executable/hash profile yet)\n";
    if (command == "--extract") {
        image.extract(destination);
        startup.log("EXTRACT: completed, files_copied=" +
                    std::to_string(info.files.size() - info.skipped_files) +
                    " files_omitted=" + std::to_string(info.skipped_files) +
                    " expected_output_bytes=" + std::to_string(info.extract_bytes) +
                    " verified_edition=none (not a playable install)");
        std::cout << "Extracted to: " << destination.string() << '\n'
                  << "Not a playable game installation: no edition validated.\n";
    } else {
        for (const auto& file : info.files)
            std::cout << file.relative_path.u8string() << " (" << file.size_bytes
                      << " bytes)"
                      << (file.skipped_by_cd_recipe ? " [SKIP: historical CD recipe]" :
                          file.invalid_extent ? " [SUSPECT: invalid extent, extraction blocked]" : "")
                      << '\n';
    }
    return 0;
}

static int cli(int argc, const fs::path* args) {
    try {
        r2fix::Startup startup(r2fix::Startup::executable_path(args[0]));
        startup.initialize();
        r2fix::log_system_inventory(startup);
        startup.log("COMMAND: argument_count=" + std::to_string(argc > 0 ? argc - 1 : 0));
        if (argc == 1) {
            usage();
            return 0;
        }
        const std::string command = args[1].string();
        if (command == "--help" || command == "-h") {
            usage();
            return 0;
        }
        if (command == "--init") {
            if (argc != 2) { usage(); return 2; }
            std::cout << "Workspace: " << startup.game_dir().string() << '\n'
                      << "Configuration: " << startup.ini_path().string() << '\n'
                      << "Log: " << startup.log_path().string() << '\n';
            startup.log("INIT: workspace prepared, no playable game installed");
            return 0;
        }
        if (command == "--version") {
            std::cout << "Rayman2Fix 0.1.0-bootstrap\n";
            return 0;
        }
        if (command == "--install" || command == "--launch" ||
            command == "--verify") {
            startup.log("GAME: request refused (no verified retail edition profile)");
            std::cerr << "Not supported yet: first identify a real retail CD edition, "
                         "its file layout and executable SHA-256.\n";
            return 2;
        }
        if ((command != "--inspect" && command != "--extract") ||
            (argc < 3 || argc > (command == "--extract" ? 4 : 3))) {
            usage();
            return 2;
        }
        const fs::path dest = argc == 4 ? args[3] : startup.game_dir() / "DiscContent";
        return run(command, args[2], dest, startup);
    } catch (const std::exception& e) {
        std::cerr << "Rayman2Fix: ERROR: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return cli(argc, args.data());
}
#else
int main(int argc, char** argv) {
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return cli(argc, args.data());
}
#endif
