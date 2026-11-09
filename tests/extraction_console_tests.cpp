#include "rayman2fix/extraction_console.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static void check(bool ok, const char* error) {
    if (!ok) throw std::runtime_error(error);
}
int main() {
    try {
        std::ostringstream printed;
        r2fix::ExtractionConsole display(printed);
        r2fix::DiscInfo disc;
        disc.volume_id = "RAYMAN2";
        disc.format = "ISO 9660";
        disc.extract_bytes = 20u * 1024u * 1024u;
        disc.skipped_files = 3;
        disc.skipped_bytes = 20;
        disc.files.resize(5);
        display.step(1, 3, "Inspecting...");
        display.plan(disc);
        display.step(2, 3, "Extracting...");
        display.progress({0, 2, 0, disc.extract_bytes, {}});
        display.progress({0, 2, 1024u * 1024u, disc.extract_bytes, "data/first.dat"});
        display.progress({0, 2, 1048576u + 123u, disc.extract_bytes, "data/first.dat"});
        display.progress({1, 2, 10u * 1024u * 1024u,
                          disc.extract_bytes, "data/first.dat"});
        display.progress({2, 2, disc.extract_bytes,
                          disc.extract_bytes, "data/second.dat"});
        display.step(3, 3, "Finalizing...");
        display.summary(disc, {2, disc.extract_bytes, 3, 20}, "Rayman2Fix");
        const std::string text = printed.str();
        check(text.find("[1/3] Inspecting...") != std::string::npos, "first step shown");
        check(text.find("[2/3] Extracting...") != std::string::npos, "copy step shown");
        check(text.find("Progress: 0%") != std::string::npos, "initial progress shown");
        check(text.find("Progress: 5%") != std::string::npos, "live progress shown");
        check(text.find("Progress: 50%") != std::string::npos, "midpoint shown");
        check(text.find("Progress: 99%") != std::string::npos, "premature complete clamped");
        check(text.find("Files extracted: 2") != std::string::npos, "real copied count shown");
        check(text.find("Files excluded: 3") != std::string::npos, "excluded count shown");
        check(text.find("20.0 MiB") != std::string::npos, "byte quantity shown");
        check(text.find("Status: success (100%)") != std::string::npos,
              "success shown only in final summary");
        check(text.find("Destination: Rayman2Fix") != std::string::npos,
              "destination shown");
        bool refused = false;
        try {
            display.summary(disc, {1, disc.extract_bytes, 3, 20}, "Rayman2Fix");
        } catch (const std::exception&) { refused = true; }
        check(refused, "never show success summary for mismatched completed totals");
        std::cout << "Extraction progress console tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Extraction progress console tests failed: " << error.what() << '\n';
        return 1;
    }
}
