#include "rayman2fix/gog_offline.hpp"
#include "rayman2fix/gog_hook_diagnostics.hpp"
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
        require(std::find(args.begin(), args.end(), L"--list-sizes") != args.end(), "complete manifest");
        require(std::find(args.begin(), args.end(), L"--silent") != args.end(), "machine-readable manifest");
        const auto manifest = g::parse_extraction_manifest(
            "1469440 Rayman2.exe\r\n359 DLL\\Snd_Cpa.ini\r\n0 Data/empty file.txt\r\n");
        require(manifest.size() == 3 && manifest.at("rayman2.exe") == 1469440 &&
                manifest.at("dll/snd_cpa.ini") == 359 &&
                manifest.at("data/empty file.txt") == 0, "size manifest parsed");
        g::verify_extraction_manifest(manifest, manifest);
        auto reject_manifest = [&](const g::FileSizes& actual) {
            try { g::verify_extraction_manifest(manifest, actual); }
            catch (const std::runtime_error&) { return true; }
            return false;
        };
        auto changed = manifest;
        changed.erase("data/empty file.txt");
        require(reject_manifest(changed), "missing selected file rejected");
        changed = manifest;
        ++changed["dll/snd_cpa.ini"];
        require(reject_manifest(changed), "truncated file rejected");
        changed = manifest;
        changed["unexpected.dll"] = 12;
        require(reject_manifest(changed), "unexpected extracted file rejected");
        for (const auto* bad_manifest : {"", "Done.\n", "1 ../outside\n",
                "1 DATA/a\n1 data/A\n", "1 data//x\n", "4294967297 huge\n",
                "4294967296 a\n1 b\n", "-1 bad\n", "1 C:\\x\n"}) {
            bool rejected = false;
            try { (void)g::parse_extraction_manifest(bad_manifest); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "invalid extraction manifest refused");
        }
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
        // Exercise extraction inventory and failure paths using only synthetic
        // files. A positive game hash cannot be fabricated for CI.
        const auto payload = dir / "payload";
        fs::create_directory(payload);
        for (auto name : g::required_files) {
            auto file = payload / fs::u8path(std::string(name));
            fs::create_directories(file.parent_path());
            std::ofstream(file).put('x');
        }
        std::ofstream(payload / "goggame-1207658940.info").put('x');
        std::ofstream(payload / "fix.dll").put('x');
        auto inventory = g::inventory(payload);
        require(inventory.size() == g::required_files.size() + 2, "complete inventory");
        bool wrong_game = false;
        try { g::validate_payload(payload, inventory); }
        catch (const std::runtime_error&) { wrong_game = true; }
        require(wrong_game, "unknown game EXE refused");
        const auto staging = dir / "game.installing";
        bool stage_refused = false;
        try { g::stage_payload(payload, staging); }
        catch (const std::runtime_error&) { stage_refused = true; }
        require(stage_refused && !fs::exists(staging), "wrong EXE never staged");
        fs::remove(payload / "dll" / "snd_cpa.ini");
        bool incomplete = false;
        try { g::validate_payload(payload, g::inventory(payload)); }
        catch (const std::runtime_error&) { incomplete = true; }
        require(incomplete, "missing audio config refused");
#ifndef _WIN32
        std::ofstream(payload / "Rayman2.exe").put('x');
        bool case_collision = false;
        try { (void)g::inventory(payload); }
        catch (const std::runtime_error&) { case_collision = true; }
        require(case_collision, "case-fold collision refused");
        fs::remove(payload / "Rayman2.exe");
        fs::create_symlink(payload / "rayman2.exe", payload / "symlink");
        bool symlink_refused = false;
        try { (void)g::inventory(payload); }
        catch (const std::runtime_error&) { symlink_refused = true; }
        require(symlink_refused, "payload symlink refused");
        fs::remove(payload / "symlink");
#endif
        const auto state = dir / "state";
        fs::create_directory(state);
        const auto game = dir / "game";
        fs::create_directory(game);
        std::string problem;
        require(!g::has_receipt(game) && !g::verify_receipt(game, problem), "missing receipt");
        std::ofstream(state / g::receipt_name) << "RAYMAN2FIX-GOG-FILES\t1\nPRODUCT\t1207658940\n";
        require(g::has_receipt(game) && !g::verify_receipt(game, problem), "empty receipt");
        std::ofstream(state / ".rayman2fix-cd-files.tsv").put('x');
        require(!g::verify_receipt(game, problem) && problem.find("conflicting") != std::string::npos,
                "conflicting receipt refused");

#ifdef RAYMAN2FIX_BUNDLED_INNOEXTRACT
        require(g::OfflineWorkspace::available(), "embedded extractor available");
        {
            g::OfflineWorkspace workspace(dir);
            require(fs::is_regular_file(dir / "gog.installing/tool/innoextract.exe"), "extractor deployed");
            require(workspace.probe(child) == g::Probe::unrelated, "real innoextract rejects unrelated PE");
            bool concurrent = false;
            try { g::OfflineWorkspace second(dir); }
            catch (const std::exception&) { concurrent = true; }
            require(concurrent, "no overwrite private workspace");
        }
        require(!fs::exists(dir / "gog.installing"), "extractor workspace cleaned");
#endif
        // Synthetic PE32 with one file-backed executable section; no game
        // code or executable contents are needed to test the RVA mapper.
        std::vector<unsigned char> pe(1024);
        auto put16 = [&](std::size_t off, unsigned v) {
            pe[off]=static_cast<unsigned char>(v);
            pe[off+1]=static_cast<unsigned char>(v>>8);
        };
        auto put32 = [&](std::size_t off, unsigned v) {
            put16(off,v); put16(off+2,v>>16);
        };
        put16(0,0x5a4d); put32(0x3c,0x80);
        put32(0x80,0x4550); put16(0x84,0x14c); put16(0x86,1);
        put32(0x88,0x3807B68B); put16(0x94,0xe0);
        put16(0x98,0x10b); put32(0x98+28,0x400000);
        put32(0x98+56,0x1D7000);
        constexpr std::size_t sec=0x98+0xe0;
        put32(sec+12,0x22000); put32(sec+16,512);
        put32(sec+20,512); put32(sec+36,0x60000020);
        pe[512+8]=0x83; pe[512+9]=0xec;
        const g::HookSignature fake{"synthetic",0x22008,{0x83,0xec}};
        const g::PeDiagnostic image(pe);
        require(image.timestamp()==0x3807B68B && image.image_size()==0x1D7000 &&
                image.image_base()==0x400000, "PE32 identity");
        require(image.check(fake)=="match", "file-backed signature");
        require(image.check({"changed",0x22008,{0x90,0x90}})=="mismatch", "signature mismatch");
        require(image.check({"absent",0x50000,{0x90}})=="not-file-backed", "unmapped signature");
        put32(sec+36,0x40000040);
        require(g::PeDiagnostic(pe).check(fake)=="non-executable", "data not code");
        pe.resize(64);
        bool malformed = false;
        try { (void)g::PeDiagnostic(pe); }
        catch (const std::runtime_error&) { malformed = true; }
        require(malformed, "truncated PE refused");
        std::cout << "GOG source policy OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
