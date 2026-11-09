#pragma once

#include "rayman2fix/disc.hpp"

#include <chrono>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace r2fix {

// Human-friendly, line-oriented output suitable for PowerShell, cmd, Wine,
// redirected logs and CI (no ANSI/terminal-only control characters).
class ExtractionConsole {
public:
    explicit ExtractionConsole(std::ostream& output);
    void step(unsigned number, unsigned total, const std::string& description);
    void plan(const DiscInfo& info);
    void progress(const ExtractionProgress& progress);
    void summary(const DiscInfo& info, const ExtractionResult& result,
                 const std::filesystem::path& destination);

private:
    std::ostream& out_;
    std::chrono::steady_clock::time_point start_;
    unsigned next_percent_ = 5;
    bool began_ = false;
};

}  // namespace r2fix
