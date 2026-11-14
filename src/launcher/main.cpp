#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/disc.hpp"
#include "rayman2fix/display.hpp"
#include "rayman2fix/extraction_console.hpp"
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
        << "Install and verify CD assets, then test-launch the exact approved unprotected Rayman 2 EXE.\n\n"
        << "Usage:\n"
        << "  Rayman2Fix                          (automatic CD/installation workflow)\n"
        << "  Rayman2Fix --inspect <game.iso|game.cue>\n"
        << "  Rayman2Fix --extract <game.iso|game.cue> [destination]\n"
        << "  Rayman2Fix --verify                 (check installed CD assets)\n"
        << "  Rayman2Fix --version\n"
        << "  Rayman2Fix --help\n\n"
        << "Diagnostic --extract output: <launcher>/Rayman2Fix/DiscContent\n"
        << "Automatic installs: <launcher>/Rayman2Fix/ (staged, no silent overwrite)\n"
        << "Options: <launcher>/Rayman2Fix/Rayman2Fix.ini\n"
        << "Logs: <launcher>/Rayman2Fix_installer.log (installer/launcher)\n"
        << "      <launcher>/Rayman2Fix.log (game/runtime, after launch)\n"
        << "Game launch is enabled only for the exact SHA256-approved unprotected\n"
        << "Rayman 2 executable, with the original .bak and verified GameFrame.\n"
        << "Real Windows/Wine gameplay compatibility is still being tested.\n"
        << "SDL3 gamepad support is postponed and not required for launch.\n";
}

static int run(const std::string& command, const fs::path& disc,
               const fs::path& destination, r2fix::Startup& startup) {
    r2fix::ExtractionConsole console(std::cout);
    if (command == "--extract")
        console.step(1, 3, "Inspecting CD image...");
    const auto image = r2fix::DiscImage::open(disc);
    const auto& info = image.info();
    startup.log("DISC: filename_source=" + info.filename_source +
                " output_case=lowercase");
    startup.log("DISC: format=" + info.format + " volume=" + info.volume_id +
                " files=" + std::to_string(info.files.size()) +
                " declared_bytes=" + std::to_string(info.total_bytes) +
                " distinct_extent_bytes=" + std::to_string(info.distinct_extent_bytes) +
                " shared_extent_references=" + std::to_string(info.shared_extent_references) +
                " image_bytes=" + std::to_string(info.image_bytes) +
                " volume_bytes=" + std::to_string(info.volume_bytes) +
                " planned_extract_bytes=" + std::to_string(info.extract_bytes) +
                " cd_excluded_files=" + std::to_string(info.skipped_files) +
                " cd_excluded_bytes=" + std::to_string(info.skipped_bytes) +
                " suspect_files=" + std::to_string(info.suspect_files));
    for (const auto& file : info.files) {
        if (file.skipped_by_cd_recipe)
            startup.log("DISC-OMIT: Rayman 2 installation exclusion path=" +
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
        console.plan(info);
        console.step(2, 3, "Extracting selected CD files...");
        const auto result = image.extract(destination,
            [&](const r2fix::ExtractionProgress& progress) {
                console.progress(progress);
            });
        console.step(3, 3, "Finalizing extraction...");
        console.summary(info, result, destination);
        startup.log("EXTRACT: completed, files_copied=" +
                    std::to_string(result.copied_files) +
                    " files_omitted=" + std::to_string(result.skipped_files) +
                    " copied_bytes=" + std::to_string(result.copied_bytes) +
                    " verified_edition=none (not a playable install)");
        std::cout << "  Note: diagnostic CD extraction only; "
                     "game launch remains unvalidated.\n";
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
        r2fix::configure_launcher_dpi_awareness();
        r2fix::Startup startup(r2fix::Startup::executable_path(args[0]));
        startup.initialize();
        r2fix::log_system_inventory(startup);
        startup.log("COMMAND: argument_count=" + std::to_string(argc > 0 ? argc - 1 : 0));
        if (argc == 1)
            return r2fix::run_automatic(startup);
        const std::string command = args[1].string();
        if (command == "--help" || command == "-h") {
            usage();
            return 0;
        }

        if (command == "--version") {
            std::cout << "Rayman2Fix 0.1.0-bootstrap\n";
            return 0;
        }
        if (command == "--verify") {
            if (argc != 2) { usage(); return 2; }
            return r2fix::verify_installation(startup);
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
