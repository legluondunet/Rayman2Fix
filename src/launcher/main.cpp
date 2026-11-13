#include "rayman2fix/auto_flow.hpp"
#include "rayman2fix/display.hpp"
#include "rayman2fix/game_launch.hpp"
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
        << "Install, verify and launch Rayman 2 PC CD editions.\n\n"
        << "Usage:\n"
        << "  Rayman2Fix.exe                       automatic install/verify/launch\n"
        << "  Rayman2Fix.exe --install-only        install/verify without launching\n"
        << "  Rayman2Fix.exe --launch-no-verify    launch directly; no checks or repairs\n"
        << "  Rayman2Fix.exe --verify              verify installed CD assets\n"
        << "  Rayman2Fix.exe --version             show installer version\n"
        << "  Rayman2Fix.exe --help                show this help (-h also accepted)\n"
        << "  Rayman2Fix.exe --verbose             verbose console diagnostics\n\n"
        << "--verbose may be combined with a command, in either order.\n"
        << "Automatic install searches for exactly one .iso/.cue beside the launcher.\n"
        << "Installation directory: <launcher>/Rayman2Fix/\n"
        << "Unchecked launch does not verify executable presence, hashes, CD files,\n"
        << "or modify/repair the installation; OS errors are returned as-is.\n"
        << "Installer log: <launcher>/Rayman2Fix.log\n";
}

static int cli(int argc, const fs::path* args) {
    try {
        std::string command;
        bool verbose = false;
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
                 argument == "--launch-no-verify")) {
                command = argument;
                continue;
            }
            usage(); // also refuses legacy --inspect / --extract
            return 2;
        }

        if (command == "--help" || command == "-h") {
            usage();
            return 0;
        }
        if (command == "--version") {
            std::cout << "Rayman2Fix 0.1.0-bootstrap\n";
            return 0;
        }

        r2fix::configure_launcher_dpi_awareness();
        r2fix::Startup startup(r2fix::Startup::executable_path(args[0]));
        startup.set_verbose(verbose);
        const bool unchecked = command == "--launch-no-verify";
        // Direct launch does not initialize, repair or extract game assets.
        startup.initialize(!unchecked);
        r2fix::log_system_inventory(startup);
        startup.log("COMMAND: name=" + (command.empty() ? "automatic" : command) +
                    " verbose=" + (verbose ? "1" : "0"));
        if (unchecked)
            return r2fix::launch_verified_game(startup, false);
        if (command == "--verify")
            return r2fix::verify_installation(startup);
        if (command == "--install-only")
            return r2fix::run_automatic(startup, true);
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
