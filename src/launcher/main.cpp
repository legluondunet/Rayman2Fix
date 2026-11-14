#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/disc.hpp"
#include "rayman2fix/display.hpp"
#include "rayman2fix/game_launch.hpp"
#include "rayman2fix/startup.hpp"

#include <filesystem>
#include <fstream>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage() {
    std::cout
        << "Rayman2Fix 0.1.0 (bootstrap)\n"
        << "Install, verify and launch Rayman 2 PC CD editions.\n\n"
        << "Usage:\n"
        << "  Rayman2Fix.exe                       automatic install/verify/launch\n"
        << "  Rayman2Fix.exe --install-only        install/verify/upgrade without launching\n"
        << "  Rayman2Fix.exe --launch-no-verify    launch directly; no checks or repairs\n"
        << "  Rayman2Fix.exe --verify              verify installed CD assets\n"
        << "  Rayman2Fix.exe --inspect image.iso  inspect unknown CD; write Rayman2Fix_inspect.log\n"
        << "  Rayman2Fix.exe --inspect image.cue [track.bin|track.img]\n"
        << "  Rayman2Fix.exe --version             show installer version\n"
        << "  Rayman2Fix.exe --help                show this help (-h also accepted)\n"
        << "  Rayman2Fix.exe --verbose             verbose console diagnostics\n\n"
        << "--verbose may be combined with a command, in either order.\n"
        << "Automatic install searches for exactly one .iso/.cue beside the launcher.\n"
        << "Installation directory: <launcher>/Rayman2Fix/\n"
        << "Unchecked launch does not verify executable presence, hashes, CD files,\n"
        << "or modify/repair the installation; OS errors are returned as-is.\n"
        << "Options: <launcher>/Rayman2Fix/Rayman2Fix.ini\n"\n        << "Installer log: <launcher>/Rayman2Fix_installer.log\n"\n        << "Game/runtime log: <launcher>/Rayman2Fix.log\n";
}

static int inspect_cd(const fs::path& executable, const std::vector<fs::path>& media,
                      bool verbose) {
    const fs::path base = r2fix::Startup::executable_path(executable).parent_path();
    const fs::path report = base / "Rayman2Fix_inspect.log";
    if (fs::exists(report) && (!fs::is_regular_file(report) || fs::is_symlink(report)))
        throw std::runtime_error("inspect report path is not a regular file");
    std::ofstream log(report, std::ios::binary | std::ios::trunc);
    if (!log) throw std::runtime_error("cannot write Rayman2Fix_inspect.log");
    log << "Rayman2Fix INSPECT v1\n";
    log << "Input=" << media[0].u8string() << "\n";
    if (media.size()==2) log << "DataTrack=" << media[1].u8string() << "\n";
    int result=0;
    try {
        const fs::path source=media[0].is_absolute()?media[0]:base/media[0];
        const fs::path track=media.size()==2?
            (media[1].is_absolute()?media[1]:base/media[1]):fs::path{};
        const auto image = r2fix::DiscImage::open(source, track);
        const auto& info=image.info();
        log << "Volume=" << info.volume_id << "\nFormat=" << info.format
            << "\nFilenameSource=" << info.filename_source
            << "\nImageBytes=" << info.image_bytes
            << "\nVolumeBytes=" << info.volume_bytes
            << "\nFiles=" << info.files.size()
            << "\nSkippedFiles=" << info.skipped_files
            << "\nSuspectFiles=" << info.suspect_files << "\n";
        log << "FILE\tSIZE\tLBA\tSTATUS\n";
        for (const auto& entry:info.files) {
            log << entry.relative_path.u8string() << '\t'
                << entry.size_bytes << '\t' << entry.extent_lba << '\t'
                << (entry.invalid_extent ? "SUSPECT: " + entry.invalid_reason :
                    entry.skipped_by_cd_recipe ? "KNOWN-EXCLUSION" : "OK") << '\n';
            if (verbose)
                std::cout << "INSPECT: " << entry.relative_path.u8string()
                          << " bytes=" << entry.size_bytes << '\n';
        }
        log << "INSPECT_RESULT=COMPLETE\n";
    } catch (const std::exception& error) {
        log << "INSPECT_RESULT=FAILED\nError=" << error.what() << '\n';
        std::cerr << "Rayman2Fix: inspection failed: " << error.what() << '\n';
        result=2;
    }
    log.flush();
    if (!log) throw std::runtime_error("failed to finish inspect report");
    std::cout << "Inspection report: " << report.u8string() << '\n';
    return result;
}

static int cli(int argc, const fs::path* args) {
    try {
        std::string command;
        bool verbose = false;
        std::vector<fs::path> media;
        for (int i=1; i<argc; ++i) {
            const std::string argument = args[i].string();
            if (argument == "--verbose" && !verbose) {
                verbose = true;
                continue;
            }
            if (command.empty() &&
                (argument == "--help" || argument == "-h" ||
                 argument == "--version" || argument == "--verify" ||
                 argument == "--install-only" ||
                 argument == "--launch-no-verify" || argument == "--inspect")) {
                command = argument;
                continue;
            }
            if (command == "--inspect" && !argument.empty() &&
                argument[0] != '-' && media.size()<2) {
                media.push_back(args[i]);
                continue;
            }
            usage();
            return 2;
        }
        if (command == "--inspect") {
            if (media.empty() || media.size()>2) {usage();return 2;}
            const auto extension=media.front().extension().string();
            auto lower=extension;
            for (auto& ch:lower) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (lower!=".iso" && lower!=".cue") {usage();return 2;}
            if (media.size()==2) {
                auto track_ext=media.back().extension().string();
                for (auto& ch:track_ext) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower!=".cue" || (track_ext!=".bin" && track_ext!=".img")) {
                    usage();return 2;
                }
            }
            return inspect_cd(args[0],media,verbose);
        }
        if (!media.empty()) {usage();return 2;}
        if (command == "--help" || command == "-h") {usage();return 0;}
        if (command == "--version") {
            std::cout << "Rayman2Fix 0.1.0-bootstrap\n";return 0;
        }

        r2fix::configure_launcher_dpi_awareness();
        r2fix::Startup startup(r2fix::Startup::executable_path(args[0]));
        startup.set_verbose(verbose);
        const bool unchecked = command == "--launch-no-verify";
        startup.initialize(!unchecked);
        r2fix::log_system_inventory(startup);
        startup.log("COMMAND: name=" + (command.empty() ? "automatic" : command) +
                    " verbose=" + (verbose ? "1" : "0"));
        if (unchecked) return r2fix::launch_verified_game(startup, false);
        if (command == "--verify") return r2fix::verify_installation(startup);
        if (command == "--install-only") return r2fix::run_automatic(startup, true);
        return r2fix::run_automatic(startup);
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
