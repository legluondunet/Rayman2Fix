#include "rayman2fix/exe_patch.hpp"
#include "rayman2fix/sha256.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace r2fix {
namespace {
constexpr unsigned char kMagic[8] = {'R','2','F','X','P','0','1',0};
constexpr std::size_t kHeaderSize = 88;
constexpr std::uint64_t kMaxExe = 64ull * 1024 * 1024;
constexpr std::size_t kMaxPatch = 512u * 1024;
constexpr std::size_t kMaxLiterals = 64u * 1024;

std::uint64_t le64(const unsigned char* p) {
    std::uint64_t n = 0;
    for (int i = 7; i >= 0; --i) n = (n << 8) | p[i];
    return n;
}
std::uint32_t le32(const unsigned char* p) {
    std::uint32_t n = 0;
    for (int i = 3; i >= 0; --i) n = (n << 8) | p[i];
    return n;
}
std::string hex(const unsigned char* p, std::size_t n) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string value;
    value.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        value += alphabet[(p[i] >> 4) & 0xfu];
        value += alphabet[p[i] & 0xfu];
    }
    return value;
}
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("executable patch: " + message);
}

// Decode a raw LZ4 block without an external library. Limits are checked
// against the trusted target size from the signed-off SHA256 patch header.
// Z commands carry [decoded_bytes:u32][encoded_bytes:u32][LZ4 block].
std::vector<char> decompress_lz4(const unsigned char* block, std::size_t length,
                                  std::uint32_t expected) {
    std::vector<char> decoded;
    decoded.reserve(expected);
    std::size_t p = 0;
    auto add_length = [&](std::size_t value) {
        if (value == 15) {
            while (true) {
                if (p >= length) fail("truncated LZ4 length extension");
                const auto n = block[p++];
                if (value > expected || n > expected - value)
                    fail("LZ4 decoded size overflow");
                value += n;
                if (n != 255) break;
            }
        }
        return value;
    };
    while (p < length) {
        const unsigned char token = block[p++];
        const auto literals = add_length(token >> 4);
        if (literals > expected - decoded.size() || literals > length - p)
            fail("LZ4 literal length exceeds input or expected output");
        decoded.insert(decoded.end(),
                       reinterpret_cast<const char*>(block + p),
                       reinterpret_cast<const char*>(block + p + literals));
        p += literals;
        if (p == length) break;
        if (length - p < 2) fail("missing LZ4 match offset");
        const auto offset = static_cast<std::size_t>(block[p]) |
                            (static_cast<std::size_t>(block[p + 1]) << 8);
        p += 2;
        if (!offset || offset > decoded.size()) fail("invalid LZ4 back-reference");
        auto count = add_length(token & 15);
        if (expected - decoded.size() < 4 ||
            count > expected - decoded.size() - 4)
            fail("LZ4 match overflows expected result");
        count += 4;
        for (std::size_t n = 0; n < count; ++n)
            decoded.push_back(decoded[decoded.size() - offset]);
    }
    if (decoded.size() != expected) fail("LZ4 decompression output size mismatch");
    return decoded;
}
}

ExePatchHeader inspect_exe_patch(const std::vector<unsigned char>& patch) {
    if (patch.size() < kHeaderSize + 1 || patch.size() > kMaxPatch)
        fail("invalid delta size");
    if (!std::equal(kMagic, kMagic + 8, patch.begin()))
        fail("unsupported patch format");
    ExePatchHeader info;
    info.original_size = le64(patch.data() + 8);
    info.patched_size = le64(patch.data() + 16);
    info.original_sha256 = hex(patch.data() + 24, 32);
    info.patched_sha256 = hex(patch.data() + 56, 32);
    if (!info.original_size || !info.patched_size ||
        info.original_size > kMaxExe || info.patched_size > kMaxExe)
        fail("unsafe original or patched executable size");
    return info;
}

ExePatchHeader apply_exe_patch(const fs::path& original,
                              const fs::path& destination,
                              const std::vector<unsigned char>& patch) {
    const auto info = inspect_exe_patch(patch);
    if (!fs::is_regular_file(original) || fs::is_symlink(original) ||
        fs::file_size(original) != info.original_size ||
        sha256_file(original) != info.original_sha256)
        fail("original executable size or SHA256 not supported by this patch");
    if (original == destination || fs::exists(destination) || fs::is_symlink(destination))
        fail("output must not exist and must differ from original");

    std::ifstream src(original, std::ios::binary);
    if (!src) fail("cannot open original executable");
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) fail("cannot create patch destination");
    try {
        std::size_t pos = kHeaderSize;
        std::uint64_t produced = 0;
        std::size_t literal_total = 0;
        bool ended = false;
        std::array<char, 65536> buffer{};
        while (pos < patch.size()) {
            const char command = static_cast<char>(patch[pos++]);
            if (command == 'E') {
                if (pos != patch.size())
                    fail("unexpected trailing patch data");
                ended = true;
                break;
            }
            if (command == 'C') {
                if (patch.size() - pos < 12) fail("truncated source-copy operation");
                const auto offset = le64(patch.data() + pos);
                const auto count = le32(patch.data() + pos + 8);
                pos += 12;
                if (!count || offset > info.original_size ||
                    count > info.original_size - offset ||
                    count > info.patched_size - produced)
                    fail("invalid source-copy bounds");
                src.clear();
                src.seekg(static_cast<std::streamoff>(offset));
                if (!src) fail("cannot seek original executable");
                std::uint32_t remaining = count;
                while (remaining) {
                    const std::size_t n = std::min<std::size_t>(remaining, buffer.size());
                    src.read(buffer.data(), static_cast<std::streamsize>(n));
                    if (!src) fail("cannot read original executable");
                    out.write(buffer.data(), static_cast<std::streamsize>(n));
                    if (!out) fail("cannot write patched executable");
                    remaining -= static_cast<std::uint32_t>(n);
                }
                produced += count;
            } else if (command == 'Z') {
                if (patch.size() - pos < 8) fail("truncated LZ4 block header");
                const auto unpacked = le32(patch.data() + pos);
                const auto packed = le32(patch.data() + pos + 4);
                pos += 8;
                if (!unpacked || !packed || packed > patch.size() - pos ||
                    unpacked > info.patched_size - produced ||
                    packed > kMaxLiterals - literal_total)
                    fail("compressed literal too large or truncated");
                const auto block = decompress_lz4(patch.data() + pos, packed, unpacked);
                out.write(block.data(), static_cast<std::streamsize>(block.size()));
                if (!out) fail("cannot write uncompressed LZ4 bytes");
                pos += packed;
                literal_total += packed;
                produced += unpacked;
            } else if (command == 'L') {
                if (patch.size() - pos < 4) fail("truncated literal-length field");
                const auto count = le32(patch.data() + pos);
                pos += 4;
                if (!count || count > patch.size() - pos ||
                    count > info.patched_size - produced ||
                    count > kMaxLiterals - literal_total)
                    fail("literal patch payload too large or truncated");
                out.write(reinterpret_cast<const char*>(patch.data() + pos),
                          static_cast<std::streamsize>(count));
                if (!out) fail("cannot write patched literal");
                pos += count;
                literal_total += count;
                produced += count;
            } else {
                fail("unsupported patch operation");
            }
        }
        if (!ended || produced != info.patched_size)
            fail("incomplete patched output");
        out.flush();
        if (!out) fail("failed flushing patched executable");
        out.close();
        if (!out) fail("failed closing patched executable");
        if (fs::file_size(destination) != info.patched_size ||
            sha256_file(destination) != info.patched_sha256)
            fail("patched output SHA256 mismatch");
        fs::permissions(destination, fs::perms::owner_write, fs::perm_options::add);
        return info;
    } catch (...) {
        out.close();
        std::error_code ignored;
        fs::remove(destination, ignored);
        throw;
    }
}

std::vector<unsigned char> bundled_exe_patch() {
#ifdef _WIN32
    const HRSRC resource = FindResourceW(
        nullptr, MAKEINTRESOURCEW(1102), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!resource) return {};
    const auto length = SizeofResource(nullptr, resource);
    if (!length || length > kMaxPatch)
        fail("bundled patch size is invalid");
    const auto payload = LoadResource(nullptr, resource);
    const auto* bytes = payload ?
        static_cast<const unsigned char*>(LockResource(payload)) : nullptr;
    if (!bytes) fail("bundled patch resource is unreadable");
    return {bytes, bytes + length};
#else
    return {};
#endif
}

std::vector<unsigned char> available_exe_patch(const fs::path& launcher_root) {
    const auto embedded = bundled_exe_patch();
    if (!embedded.empty()) return embedded;

    // Development-only external delta. This lets the owner validate a
    // byte-perfect patch against their own CD before bundling it in one EXE.
    // Never load symlinks or arbitrarily large files, and never overwrite
    // a game unless the embedded target SHA256 allowlist also matches.
    const auto path = launcher_root / "Rayman2Fix-executable.r2patch";
    if (!fs::exists(path)) return {};
    if (!fs::is_regular_file(path) || fs::is_symlink(path) ||
        fs::file_size(path) > kMaxPatch)
        fail("unrecognized local executable patch sidecar");
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot read local executable delta");
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    inspect_exe_patch(data);
    return data;
}

} // namespace r2fix
