#include "rayman2fix/exe_patch.hpp"
#include "rayman2fix/sha256.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void check(bool yes, const char* reason) {
    if (!yes) throw std::runtime_error(reason);
}
static void append64(std::vector<unsigned char>& v, std::uint64_t n) {
    for (unsigned j = 0; j < 8; ++j)
        v.push_back(static_cast<unsigned char>(n >> (8 * j)));
}
static void append32(std::vector<unsigned char>& v, std::uint32_t n) {
    for (unsigned j = 0; j < 4; ++j)
        v.push_back(static_cast<unsigned char>(n >> (8 * j)));
}
static void digest(std::vector<unsigned char>& v, const std::string& hex) {
    for (std::size_t i = 0; i < hex.size(); i += 2)
        v.push_back(static_cast<unsigned char>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
}
static void write(const fs::path& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(f), "failed creating synthetic exe");
}
static std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static std::vector<unsigned char> fixture(const std::string& source,
                                           const std::string& target) {
    const std::string prefix = "ABCDEFGH";
    const std::string suffix = "ENDINGS";
    check(source == prefix + suffix, "bad test fixture input");
    check(target == prefix + "new" + suffix, "bad test fixture target");
    std::vector<unsigned char> data{
        'R','2','F','X','P','0','1',0
    };
    append64(data, source.size());
    append64(data, target.size());
    digest(data, r2fix::sha256_text(source));
    digest(data, r2fix::sha256_text(target));
    // Reproduce "ABCDEFGH", then insert "new", then copy "ENDINGS".
    data.push_back('C'); append64(data, 0); append32(data, 8);
    data.push_back('L'); append32(data, 3);
    data.insert(data.end(), {'n','e','w'});
    data.push_back('C'); append64(data, 8); append32(data, 7);
    data.push_back('E');
    return data;
}

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("r2fix-exe-patch-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        const std::string source = "ABCDEFGHENDINGS";
        const std::string target = "ABCDEFGHnewENDINGS";
        const auto patch = fixture(source, target);
        const auto header = r2fix::inspect_exe_patch(patch);
        check(header.original_size == source.size() &&
              header.patched_size == target.size(), "header sizes");
        check(header.original_sha256 == r2fix::sha256_text(source), "original hash");
        check(header.patched_sha256 == r2fix::sha256_text(target), "patched hash");
        const fs::path original = root / "original.exe";
        const fs::path output = root / "patched.exe";
        write(original, source);
        const auto result = r2fix::apply_exe_patch(original, output, patch);
        check(result.patched_sha256 == r2fix::sha256_text(target), "result hash");
        check(read(output) == target, "patch faithfully reconstructs target");
        check(read(original) == source, "original source was not modified");

        bool refused = false;
        try { r2fix::apply_exe_patch(original, output, patch); }
        catch (const std::exception&) { refused = true; }
        check(refused && read(output) == target, "never replace prior output");

        write(root / "unrecognized.exe", "unexpected");
        refused = false;
        try {
            r2fix::apply_exe_patch(root / "unrecognized.exe",
                                  root / "blocked.exe", patch);
        } catch (const std::exception&) { refused = true; }
        check(refused && !fs::exists(root / "blocked.exe"),
              "wrong retail source SHA256 must never create an output");

        auto tampered = patch;
        tampered[tampered.size() - 3] ^= 1;
        refused = false;
        try {
            r2fix::apply_exe_patch(original, root / "bad.exe", tampered);
        } catch (const std::exception&) { refused = true; }
        check(refused && !fs::exists(root / "bad.exe"),
              "corrupt patch removes the partial output");

        std::error_code ignored;
        fs::remove_all(root, ignored);
        std::cout << "Executable patch safety tests passed\n";
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "Executable patch test failed: " << err.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
