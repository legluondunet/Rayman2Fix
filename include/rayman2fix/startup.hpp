#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace r2fix {

class Startup {
public:
    // The working tree is always relative to the *launcher executable*, never
    // a random shell working directory or the disc-image location.
    explicit Startup(const std::filesystem::path& executable);
    static std::filesystem::path executable_path(const std::filesystem::path& argv0);
    const std::filesystem::path& root() const noexcept { return root_; }
    const std::filesystem::path& game_dir() const noexcept { return game_dir_; }
    const std::filesystem::path& ini_path() const noexcept { return ini_path_; }
    const std::filesystem::path& log_path() const noexcept { return log_path_; }
    const std::filesystem::path& runtime_log_path() const noexcept { return runtime_log_path_; }

    // A direct unchecked launch must not deploy/repair DLLs or create config files.
    void initialize(bool provision_files = true);
    void set_verbose(bool enabled) noexcept { verbose_ = enabled; }
    void log(const std::string& line);
    // Reset the dedicated DLL/game log just before an approved game launch.
    void prepare_runtime_log();
    bool has_runtime_dll() const;
    bool logging_enabled() const noexcept { return logging_enabled_; }
private:
    std::filesystem::path root_, game_dir_, ini_path_, log_path_, runtime_log_path_;
    std::ofstream log_stream_;
    bool logging_enabled_ = true;
    bool verbose_ = false;
};

void log_system_inventory(Startup& startup);

} // namespace r2fix
