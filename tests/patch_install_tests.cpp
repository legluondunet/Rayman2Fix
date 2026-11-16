#include "rayman2fix/patch_install.hpp"
#include "rayman2fix/sha256.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static void require(bool yes, const char* message) {
    if (!yes) throw std::runtime_error(message);
}
static void append64(std::vector<unsigned char>& out, std::uint64_t n) {
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(static_cast<unsigned char>(n >> (8 * i)));
}
static void append32(std::vector<unsigned char>& out, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<unsigned char>(n >> (8 * i)));
}
static void append_digest(std::vector<unsigned char>& out, std::string hex) {
    for (std::size_t i = 0; i < hex.size(); i += 2)
        out.push_back(static_cast<unsigned char>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
}
static std::vector<unsigned char> sample_patch(const std::string& source,
                                                 const std::string& target) {
    std::vector<unsigned char> patch{'R','2','F','X','P','0','1',0};
    append64(patch, source.size());
    append64(patch, target.size());
    append_digest(patch, r2fix::sha256_text(source));
    append_digest(patch, r2fix::sha256_text(target));
    patch.push_back('C'); append64(patch, 0); append32(patch, 8);
    patch.push_back('L'); append32(patch, 3);
    patch.insert(patch.end(), {'n','e','w'});
    patch.push_back('C'); append64(patch, 8); append32(patch, 7);
    patch.push_back('E');
    return patch;
}
static void write(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    require(static_cast<bool>(out), "cannot create test file");
}
static std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("r2fix-backup-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        // The approved local decrypted file uses the same atomic backup,
        // patch receipt and SHA256 checks as the older private .r2patch flow.
        {
            const fs::path direct = root / "local-decrypted";
            fs::create_directories(direct);
            const std::string source = "ORIGINAL_PROTECTED";
            const std::string target = "APPROVED_UNPROTECTED";
            const fs::path original = direct / "rayman2.exe";
            const fs::path supplied = root / "Rayman2.exe";
            const fs::path marker = direct / "rayman2fix" / ".rayman2fix-exe-patch.tsv";
            const r2fix::ExePatchHeader approved{
                source.size(), target.size(),
                r2fix::sha256_text(source), r2fix::sha256_text(target)
            };
            write(original, source);
            write(supplied, "BAD-EXECUTABLE");
            bool refused = false;
            try { r2fix::install_exe_replacement_from_file(direct, supplied, approved); }
            catch (const std::exception&) { refused = true; }
            require(refused && read(original) == source &&
                    !fs::exists(direct / "rayman2.exe.bak") && !fs::exists(marker),
                    "invalid local EXE fails closed with protected original untouched");
            write(supplied, target);
            const auto installed = r2fix::install_exe_replacement_from_file(
                direct, supplied, approved);
            require(installed.patched_sha256 == approved.patched_sha256,
                    "locally supplied unprotected target verified");
            require(read(original) == target && read(supplied) == target,
                    "local decrypted EXE copied without removing user file");
            require(read(direct / "rayman2.exe.bak") == source,
                    "the original encrypted game is preserved");
            require(fs::exists(marker) && !fs::exists(direct / ".rayman2fix-exe-patch.tsv") &&
                    !fs::exists(direct / "rayman2.exe.patching"),
                    "receipt created in rayman2fix and no staging file remains");
            std::string reason;
            require(r2fix::verify_exe_replacement(direct, approved.original_sha256,
                                                 approved.original_size, reason),
                    "locally supplied decrypted executable verifies through shared receipt");
            refused = false;
            try { r2fix::install_exe_replacement_from_file(direct, supplied, approved); }
            catch (const std::exception&) { refused = true; }
            require(refused && read(direct / "rayman2.exe.bak") == source,
                    "repeat local file install never overwrites existing .bak");
            fs::remove_all(direct);
            fs::remove(supplied);
        }
        {
            const fs::path mismatch = root / "unsupported-protected";
            fs::create_directories(mismatch);
            const std::string source = "GOOD_PROTECTED";
            const std::string target = "GOOD_DECRYPTED";
            const fs::path supplied = root / "Rayman2.exe";
            write(mismatch / "rayman2.exe", "WRONG_PROTECTED");
            write(supplied, target);
            const r2fix::ExePatchHeader approved{
                source.size(), target.size(),
                r2fix::sha256_text(source), r2fix::sha256_text(target)
            };
            bool refused = false;
            try { r2fix::install_exe_replacement_from_file(mismatch, supplied, approved); }
            catch (const std::exception&) { refused = true; }
            require(refused && read(mismatch / "rayman2.exe") == "WRONG_PROTECTED" &&
                    !fs::exists(mismatch / "rayman2.exe.bak"),
                    "unsupported protected source is never overwritten");
            fs::remove(supplied);
            fs::remove_all(mismatch);
        }
        const std::string cd_exe = "ABCDEFGHENDINGS";
        const std::string unprotected = "ABCDEFGHnewENDINGS";
        const auto patch = sample_patch(cd_exe, unprotected);
        const auto hash = r2fix::sha256_text(cd_exe);
        const fs::path exe = root / "rayman2.exe";
        const fs::path backup = root / "rayman2.exe.bak";
        const fs::path marker = root / "rayman2fix" / ".rayman2fix-exe-patch.tsv";
        write(exe, cd_exe);
        std::string error;
        require(r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "unpatched original should verify");
        const auto result = r2fix::install_exe_replacement(root, patch);
        require(result.original_sha256 == hash, "original hash stored");
        require(fs::exists(backup), "CD executable preserved under .bak");
        require(read(backup) == cd_exe, "backup matches original exactly");
        require(read(exe) == unprotected, "new rayman2.exe is patched output");
        require(fs::exists(marker), "patch receipt generated in private fix directory");
        require(!fs::exists(root / ".rayman2fix-exe-patch.tsv"),
                "game root must not hold a fix-only patch receipt");
        require(r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "original backup and patched exe both verify");
        require(!fs::exists(root / "rayman2.exe.patching"),
                "temporary output removed after patching");

        bool refused = false;
        try { r2fix::install_exe_replacement(root, patch); }
        catch (const std::exception&) { refused = true; }
        require(refused && read(backup) == cd_exe,
                "second patch attempt must not replace existing original backup");

        write(exe, "ABCDEFGHzzzENDINGS");
        require(!r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "same-size modified patched EXE must be rejected");
        write(exe, unprotected);
        require(r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "patched EXE restored");
        write(backup, "ABCDEFGHzzzINGS");
        require(!r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "damaged backup must be rejected");
        write(backup, cd_exe);

        fs::remove(marker);
        require(!r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "stray .bak without a patch marker must be rejected");
        fs::remove(backup);
        write(exe, cd_exe);
        require(r2fix::verify_exe_replacement(root, hash, cd_exe.size(), error),
                "unpatched original again verifies");

        write(backup, "USERBACKUP");
        refused = false;
        try { r2fix::install_exe_replacement(root, patch); }
        catch (const std::exception&) { refused = true; }
        require(refused && read(backup) == "USERBACKUP" &&
                    read(exe) == cd_exe && !fs::exists(marker),
                "preexisting .bak must never be overwritten or discarded");

        fs::remove_all(root);
        std::cout << "Original .bak / patched EXE safety tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cerr << "Original backup test failed: " << e.what() << '\n';
        return 1;
    }
}
