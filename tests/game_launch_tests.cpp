#include "rayman2fix/game_launch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void write(const fs::path& target, const std::string& content) {
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) throw std::runtime_error("cannot write test file");
}

int main(int argc, char** argv) {
    const auto root = fs::temp_directory_path() /
        ("r2fix process with spaces " +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto preflight = r2fix::check_game_launch(root);
        check(!preflight.allowed, "empty installation must be refused");
        write(root / "rayman2.exe", "untrusted executable");
        write(root / "rayman2.exe.bak", "untrusted backup");
        write(root / ".rayman2fix-exe-patch.tsv", "invalid patch marker");
        write(root / "ubi.ini", "[Rayman2]\nGLI_Dll=Glide2\n");
        check(!r2fix::check_game_launch(root).allowed,
              "tampered executable/marker must not be allowed to launch");

#ifdef _WIN32
        const auto child = fs::absolute(fs::path(argv[0])).parent_path() /
                           "test_child_process.exe";
        check(fs::exists(child), "test child executable not built");
        const auto result = r2fix::run_child_process(child, root);
        check(result.started && result.error_code == 0 &&
              result.exit_code == 17,
              "child process launched and nonzero exit status captured");
        check(fs::exists(root / "child-working-directory.txt"),
              "child inherited explicit game working directory");
        const auto nonexistent = root / "does-not-exist.exe";
        const auto failure = r2fix::run_child_process(nonexistent, root);
        check(!failure.started && failure.error_code != 0,
              "missing executable must fail with an OS error");
#else
        (void)argc;
        (void)argv;
        const auto failure = r2fix::run_child_process(root / "missing.exe", root);
        check(!failure.started, "non-Windows tests do not spawn Windows EXEs");
#endif
        fs::remove_all(root);
        std::cout << "Launch preflight / process tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cerr << "Launch tests failed: " << e.what() << '\n';
        return 1;
    }
}
