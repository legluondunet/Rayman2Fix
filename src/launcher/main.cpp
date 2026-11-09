#include "rayman2fix/disc.hpp"

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
        << "  Rayman2Fix --version\n"
        << "  Rayman2Fix --help\n\n"
        << "Default extraction destination: ./Rayman2Fix/DiscContent\n"
        << "Files are copied offline from a disc image you own. The game is NOT\n"
        << "installed, patched, made DRM-free or launched by this bootstrap.\n";
}

static int run(const std::string& command, const fs::path& disc,
               const fs::path& destination) {
    const auto image = r2fix::DiscImage::open(disc);
    const auto& info = image.info();
    std::cout << "Disc image: " << disc.string() << '\n'
              << "Format: " << info.format << '\n'
              << "ISO volume: " << info.volume_id << '\n'
              << "Files: " << info.files.size() << '\n'
              << "Content bytes: " << info.total_bytes << '\n'
              << "Edition: UNKNOWN (no trusted executable/hash profile yet)\n";
    if (command == "--extract") {
        image.extract(destination);
        std::cout << "Extracted to: " << destination.string() << '\n'
                  << "Not a playable game installation: no edition validated.\n";
    } else {
        for (const auto& file : info.files)
            std::cout << file.relative_path.string() << " (" << file.size_bytes << " bytes)\n";
    }
    return 0;
}

static int cli(int argc, const fs::path* args) {
    try {
        if (argc == 1) {
            usage();
            return 0;
        }
        const std::string command = args[1].string();
        if (command == "--help" || command == "-h") {
            usage();
            return 0;
        }
        if (command == "--version") {
            std::cout << "Rayman2Fix 0.1.0-bootstrap\n";
            return 0;
        }
        if (command == "--install" || command == "--launch" ||
            command == "--verify") {
            std::cerr << "Not supported yet: first identify a real retail CD edition, "
                         "its file layout and executable SHA-256.\n";
            return 2;
        }
        if ((command != "--inspect" && command != "--extract") ||
            (argc < 3 || argc > (command == "--extract" ? 4 : 3))) {
            usage();
            return 2;
        }
        const fs::path dest = argc == 4 ? args[3] : fs::path("Rayman2Fix") / "DiscContent";
        return run(command, args[2], dest);
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
