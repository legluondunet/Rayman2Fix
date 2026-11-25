#pragma once
#include "rayman2fix/gog_extract_policy.hpp"
#include <chrono>

namespace r2fix::gog {
struct ExtractorResult {
    unsigned long exit_code;
    std::string output;
    std::string errors;
};

// executable and capture_dir belong to a private launcher-owned staging
// workspace, never to the source archive. expected_sha256 is supplied by
// the build's dependency pin, not by the user or game package.
ExtractorResult run_verified_extractor(
    const fs::path& executable, std::string_view expected_sha256,
    const Args& args, const fs::path& capture_dir,
    std::chrono::seconds timeout);
}
