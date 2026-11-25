#include "rayman2fix/gog_extractor.hpp"
#include "rayman2fix/sha256.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace g = r2fix::gog;
namespace fs = std::filesystem;
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--synthetic-child") {
        std::cout << argv[2] << '|' << argv[3];
        std::cerr << "synthetic stderr";
        return 7;
    }
    try {
        const fs::path french = "setup_rayman2_french_86383.exe";
        const fs::path renamed = "renamed package.EXE";
        const fs::path polish = "another release (polish).exe";
        const fs::path other = "unrelated.exe";
        const fs::path bad = "broken.exe";
        const fs::path cd = "original.iso";
        const auto probe = [&](const fs::path& p) {
            if (p == bad) return g::Probe::unreadable;
            if (p == other) return g::Probe::unrelated;
            return g::Probe::rayman2;
        };
        require(g::select({}, {french}, probe).source == g::Source::gog, "French");
        require(g::select({}, {renamed}, probe).path == renamed, "arbitrary name");
        require(g::select({}, {polish}, probe).source == g::Source::gog, "Polish");
        require(g::select({}, {other, renamed}, probe).path == renamed, "ignore unrelated");
        require(g::select({cd}, {other}, probe).source == g::Source::cd, "CD unchanged");
        require(g::select({cd}, {renamed}, probe).source == g::Source::ambiguous, "mixed sources");
        require(g::select({}, {french, polish}, probe).source == g::Source::ambiguous, "two setups");
        require(g::select({}, {other}, probe).source == g::Source::missing, "no source");
        require(g::select({}, {bad}, probe).source == g::Source::unreadable, "failed probe");
        require(g::select({cd}, {bad}, probe).source == g::Source::unreadable, "no silent fallback");
        require(g::matches_product("1207658940\r\n"), "ID");
        require(!g::matches_product("12076589401"), "wrong ID");
        require(!g::matches_product("fake 1207658940"), "not exact ID");
        require(!g::matches_product(""), "missing ID");
        require(g::matches_executable(g::executable_size, g::executable_sha256), "game profile");
        require(!g::matches_executable(g::executable_size + 1, g::executable_sha256), "wrong size");
        require(!g::matches_executable(g::executable_size, "unknown"), "wrong hash");

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto dir = fs::temp_directory_path() / ("r2fix-gog-policy-" + std::to_string(stamp));
        require(fs::create_directory(dir), "private temp directory");
        struct Cleanup { fs::path p; ~Cleanup(){ std::error_code ec; fs::remove_all(p, ec); } } cleanup{dir};
        for (const auto& p : {fs::path("Rayman2Fix.exe"), renamed, other, fs::path("part-1.bin")})
            std::ofstream(dir / p).put('x');
        fs::create_directory(dir / "directory.exe");
        const auto found = g::candidates(dir, dir / "Rayman2Fix.exe");
        require(found.size() == 2, "only regular EXEs other than launcher");
        require(std::find(found.begin(), found.end(), dir / renamed) != found.end(), "renamed discovered");
        const auto args = g::extract_args(dir / renamed, dir / "game.installing");
        require(std::find(args.begin(), args.end(), L"--test") != args.end(), "strict checksum");
        require(std::find(args.begin(), args.end(), L"--collisions=error") != args.end(), "collision refusal");
        require(std::find(args.begin(), args.end(), L"--dump") == args.end(), "no unsafe dump");
        require(std::find(args.begin(), args.end(), L"--gog") == args.end(), "no external rar");
        require(args.back() == fs::absolute(dir / renamed).wstring(), "absolute source");
        const auto lang = g::extract_args(dir / polish, dir / "staging", L"pl-PL");
        require(std::find(lang.begin(), lang.end(), L"pl-PL") != lang.end(), "explicit metadata language");
        require(g::quote_arg(L"some path") == L"\"some path\"", "quote spaces");
        require(g::quote_arg(L"trailing\\") == L"\"trailing\\\\\"", "quote terminal slash");
        require(g::quote_arg(L"a\"b") == L"\"a\\\"b\"", "quote embedded quote");
        require(g::normalized_path("DLL/Snd_Cpa.ini") == "dll/snd_cpa.ini", "normalize mixed case");
        for (const auto* unsafe : {"../x", "data/../x", "data/con.txt", "data/stream:alt", "data/x.", "data/x "}) {
            bool refused = false;
            try { (void)g::normalized_path(unsafe); }
            catch (const std::runtime_error&) { refused = true; }
            require(refused, "reject unsafe relative name");
        }
        require(g::role("dll/snd_cpa.ini") == g::FileRole::config, "native audio config");
        require(g::role("data/world/levels/menu/menu.sna") == g::FileRole::game, "game data");
        require(g::role("goggame-1207658940.info") == g::FileRole::metadata, "metadata");
        require(g::role("ddraw.dll") == g::FileRole::auxiliary, "wrapper candidate");
        require(g::role("fix.dll") == g::FileRole::unknown, "reserved fix file");
        std::set<std::string> core;
        for (auto p : g::required_files) core.emplace(p);
        require(g::has_required_files(core), "core layout");
        core.erase("dll/snd_cpa.ini");
        require(!g::has_required_files(core), "reject incomplete native audio");
        const auto self = fs::absolute(fs::u8path(argv[0]));
        const auto capture = dir / "capture";
        fs::create_directory(capture);
        bool bad_pin = false;
        try {
            (void)g::run_verified_extractor(self, std::string(64, '0'), {}, capture,
                                             std::chrono::seconds(5));
        } catch (const std::runtime_error&) { bad_pin = true; }
        require(bad_pin && fs::is_empty(capture), "bad dependency hash refuses before process");
#ifdef _WIN32
        const auto child = dir / "trusted child.exe";
        fs::copy_file(self, child);
        const auto result = g::run_verified_extractor(child, r2fix::sha256_file(child),
            {L"--synthetic-child", L"argument with spaces", L"slash\\"}, capture,
            std::chrono::seconds(15));
        require(result.exit_code == 7, "child exit code");
        require(result.output == "argument with spaces|slash\\", "child arguments and stdout");
        require(result.errors == "synthetic stderr", "separate stderr");
        bool collision = false;
        try {
            (void)g::run_verified_extractor(child, r2fix::sha256_file(child),
                    {L"--synthetic-child"}, capture, std::chrono::seconds(5));
        } catch (const std::runtime_error&) { collision = true; }
        require(collision, "capture must not overwrite");
#endif
        std::cout << "GOG source policy OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
