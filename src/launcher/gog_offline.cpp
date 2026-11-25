#include "rayman2fix/gog_offline.hpp"
#include "rayman2fix/sha256.hpp"
#include "rayman2fix/install_layout.hpp"
#include <fstream>
#include <set>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace r2fix::gog {
namespace {
constexpr auto pin = "946bd06d8b3722a791fea4d84f496139eef6faef61640924cb72f5f1998a68e1";
constexpr std::uintmax_t max_bytes = 4ull * 1024 * 1024 * 1024;
constexpr std::size_t max_files = 50000;

bool safe_entry(const fs::path& path) {
    if (fs::is_symlink(path)) return false;
#ifdef _WIN32
    const auto attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return true;
#endif
}
void cleanup(const fs::path& path) noexcept {
    // Only called for a directory this instance successfully created.
    std::error_code ec;
    fs::remove_all(path, ec);
}
} // namespace

bool OfflineWorkspace::available() noexcept {
#if defined(_WIN32) && defined(RAYMAN2FIX_BUNDLED_INNOEXTRACT)
    return true;
#else
    return false;
#endif
}

OfflineWorkspace::OfflineWorkspace(const fs::path& root) {
#if defined(_WIN32) && defined(RAYMAN2FIX_BUNDLED_INNOEXTRACT)
    workspace_ = fs::absolute(root) / "gog.installing";
    if (!safe_entry(root) || !fs::is_directory(root) ||
        !fs::create_directory(workspace_))
        throw std::runtime_error("unsafe or unresolved GOG staging workspace");
    try {
        const auto tool = workspace_ / "tool";
        if (!fs::create_directory(tool))
            throw std::runtime_error("cannot create private extractor directory");
        executable_ = tool / "innoextract.exe";
        const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(1111), MAKEINTRESOURCEW(10));
        if (!resource) throw std::runtime_error("missing bundled innoextract");
        const auto bytes = SizeofResource(nullptr, resource);
        const auto loaded = LoadResource(nullptr, resource);
        const auto* payload = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
        if (!payload || bytes != 1218560u)
            throw std::runtime_error("invalid embedded innoextract size");
        Sha256 hash;
        hash.update(payload, bytes);
        if (hash.finish() != pin)
            throw std::runtime_error("embedded innoextract digest mismatch");
        std::ofstream out(executable_, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot stage innoextract");
        out.write(payload, bytes);
        out.close();
        if (!out || sha256_file(executable_) != pin)
            throw std::runtime_error("staged innoextract digest mismatch");
    } catch (...) {
        cleanup(workspace_);
        throw;
    }
#else
    (void)root;
    throw std::runtime_error("this build does not contain the GOG extractor");
#endif
}
OfflineWorkspace::~OfflineWorkspace() { if (!workspace_.empty()) cleanup(workspace_); }

ExtractorResult OfflineWorkspace::run(const Args& args, std::chrono::seconds timeout) {
    const auto capture = workspace_ / ("capture-" + std::to_string(++invocation_));
    if (!fs::create_directory(capture))
        throw std::runtime_error("cannot create private GOG output capture");
    return run_verified_extractor(executable_, pin, args, capture, timeout);
}

Probe OfflineWorkspace::probe(const fs::path& candidate) {
    if (!safe_entry(candidate) || !fs::is_regular_file(candidate)) return Probe::unreadable;
    const auto result = run(product_args(candidate), std::chrono::seconds(30));
    if (result.exit_code == 0) {
        if (matches_product(result.output)) return Probe::rayman2;
        // An Inno installer with no product ID or a different ID is not
        // the supported GOG product.
        return Probe::unrelated;
    }
    const auto diagnostic = ascii_lower(result.errors + "\n" + result.output);
    // Ordinary adjacent PE executables are allowed (e.g. local CD EXE).
    // Unknown versions and failed reads are not silently treated as CD.
    if (diagnostic.find("not a supported inno setup installer!") != std::string::npos &&
        diagnostic.find("unexpected") == std::string::npos)
        return Probe::unrelated;
    return Probe::unreadable;
}

fs::path OfflineWorkspace::extract(const fs::path& setup) {
    const auto target = workspace_ / "payload";
    if (!fs::create_directory(target))
        throw std::runtime_error("GOG extraction target already exists");
    const auto result = run(extract_args(setup, target), std::chrono::minutes(15));
    if (result.exit_code != 0)
        throw std::runtime_error("GOG extraction/checksum verification failed (exit " +
                                 std::to_string(result.exit_code) + "): " +
                                 result.errors.substr(0, 2048));
    validate_payload(target, inventory(target));
    return target;
}

std::vector<InventoryEntry> inventory(const fs::path& extracted) {
    if (!fs::is_directory(extracted) || !safe_entry(extracted))
        throw std::runtime_error("unsafe GOG payload root");
    std::set<std::string> names;
    std::vector<InventoryEntry> out;
    std::uintmax_t total = 0;
    for (const auto& item : fs::recursive_directory_iterator(extracted)) {
        if (!safe_entry(item.path()))
            throw std::runtime_error("GOG payload contains a reparse point or symlink");
        const auto relative = item.path().lexically_relative(extracted);
        auto name = normalized_path(relative);
        if (!names.insert(name).second)
            throw std::runtime_error("GOG payload contains case-fold collision");
        if (names.size() > max_files)
            throw std::runtime_error("GOG payload exceeds inventory limit");
        if (item.is_directory()) continue;
        if (!item.is_regular_file())
            throw std::runtime_error("GOG payload contains non-regular file");
        const auto bytes = item.file_size();
        if (bytes > max_bytes - total)
            throw std::runtime_error("GOG payload exceeds size limit");
        total += bytes;
        out.push_back({item.path(), name, role(name), bytes});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.normalized < b.normalized;
    });
    return out;
}

void validate_payload(const fs::path& extracted,
                      const std::vector<InventoryEntry>& files) {
    std::set<std::string> names;
    const InventoryEntry* executable = nullptr;
    for (const auto& item : files) {
        if (!names.insert(item.normalized).second ||
            normalized_path(item.path.lexically_relative(extracted)) != item.normalized ||
            !safe_entry(item.path) || !fs::is_regular_file(item.path) ||
            fs::file_size(item.path) != item.bytes)
            throw std::runtime_error("invalid GOG payload inventory entry");
        if (item.normalized == "rayman2.exe") executable = &item;
    }
    if (!has_required_files(names) || !executable ||
        !names.count("goggame-1207658940.info"))
        throw std::runtime_error("incomplete GOG game layout");
    if (executable->bytes != executable_size ||
        !matches_executable(executable->bytes, sha256_file(executable->path)))
        throw std::runtime_error("extracted GOG executable is not an approved game build");
}

namespace {
constexpr auto receipt_header = "RAYMAN2FIX-GOG-FILES\t1";
constexpr auto receipt_product = "PRODUCT\t1207658940";
bool mutable_file(std::string_view name) {
    return name == "ubi.ini" || name == "dll/snd_cpa.ini" ||
           name.rfind("data/options/",0) == 0 ||
           name.rfind("data/savegame/",0) == 0;
}
bool native_file(std::string_view name) {
    const auto kind = role(name);
    return kind == FileRole::game || kind == FileRole::config;
}
bool safe_ancestry(const fs::path& root, const fs::path& relative) {
    auto current = root;
    if (!safe_entry(current)) return false;
    for (const auto& part : relative) {
        current /= part;
        if (!safe_entry(current)) return false;
    }
    return true;
}
bool regular(const fs::path& file) {
    return safe_entry(file) && fs::is_regular_file(file);
}
bool lower_sha(std::string_view sha) {
    return sha.size() == 64 && std::all_of(sha.begin(), sha.end(), [](char c){
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool required_immutable(const std::set<std::string>& names) {
    for (const auto name : required_files) {
        if (!mutable_file(name) && !names.count(std::string(name))) return false;
    }
    return true;
}
}
void stage_payload(const fs::path& extracted, const fs::path& staging) {
    const auto files = inventory(extracted);
    validate_payload(extracted, files);
    if (!fs::create_directory(staging))
        throw std::runtime_error("GOG game staging already exists");
    try {
        for (const auto& item : files) {
            if (!native_file(item.normalized)) continue;
            // Do not install existing save slots, even from a repacked
            // installer; the game may create its own mutable files later.
            if (item.normalized.rfind("data/savegame/",0) == 0 ||
                item.normalized.rfind("data/options/",0) == 0) continue;
            const auto dest = staging / fs::u8path(item.normalized);
            fs::create_directories(dest.parent_path());
            fs::copy_file(item.path, dest);
            if (fs::file_size(dest) != item.bytes ||
                sha256_file(dest) != sha256_file(item.path))
                throw std::runtime_error("GOG staged copy verification failed");
            fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add);
        }
        fs::create_directories(staging / "data" / "options");
        fs::create_directories(staging / "data" / "savegame");
    } catch (...) {
        cleanup(staging);
        throw;
    }
}

bool has_receipt(const fs::path& game) {
    const auto path = layout::state_for_game(game) / receipt_name;
    return fs::exists(path) || fs::is_symlink(path);
}

void write_receipt(const fs::path& staged) {
    const auto files = inventory(staged);
    std::set<std::string> names;
    for (const auto& item : files) names.insert(item.normalized);
    if (!has_required_files(names) ||
        !matches_executable(fs::file_size(staged / "rayman2.exe"),
                            sha256_file(staged / "rayman2.exe")))
        throw std::runtime_error("cannot receipt unapproved GOG game");
    const auto state = layout::state_for_game(staged);
    if (!fs::create_directory(state))
        throw std::runtime_error("GOG state staging already exists");
    try {
        std::ofstream out(state / receipt_name, std::ios::binary);
        if (!out) throw std::runtime_error("cannot write GOG receipt");
        out << receipt_header << '\n' << receipt_product << '\n';
        for (const auto& item : files) {
            if (!native_file(item.normalized) || mutable_file(item.normalized)) continue;
            out << "FILE\t" << item.bytes << '\t' << sha256_file(item.path)
                << '\t' << item.normalized << '\n';
        }
        out.close();
        if (!out) throw std::runtime_error("cannot complete GOG receipt");
    } catch (...) {
        cleanup(state);
        throw;
    }
}

bool verify_receipt(const fs::path& game, std::string& problem) {
    try {
        const auto state = layout::state_for_game(game);
        const auto receipt = state / receipt_name;
        if (!safe_entry(state) || !regular(receipt) ||
            fs::file_size(receipt) > 16u*1024u*1024u)
            throw std::runtime_error("missing or unsafe GOG receipt");
        if (fs::exists(layout::cd_receipt(game)) ||
            fs::is_symlink(layout::cd_receipt(game)) ||
            fs::exists(layout::exe_receipt(game)) ||
            fs::is_symlink(layout::exe_receipt(game)))
            throw std::runtime_error("conflicting CD and GOG receipts");
        std::ifstream in(receipt, std::ios::binary);
        std::string line;
        if (!std::getline(in,line) || line != receipt_header ||
            !std::getline(in,line) || line != receipt_product)
            throw std::runtime_error("unrecognized GOG receipt");
        std::set<std::string> names;
        while (std::getline(in,line)) {
            if (line.size() > 8192 || line.rfind("FILE\t",0) != 0)
                throw std::runtime_error("invalid GOG receipt line");
            auto size_end = line.find('\t',5);
            if (size_end == std::string::npos)
                throw std::runtime_error("invalid GOG receipt size");
            auto hash_end = line.find('\t',size_end+1);
            if (hash_end == std::string::npos)
                throw std::runtime_error("invalid GOG receipt hash");
            const auto size = line.substr(5,size_end-5);
            const auto hash = line.substr(size_end+1,hash_end-size_end-1);
            const auto name = line.substr(hash_end+1);
            const auto relative = fs::u8path(name);
            if (size.empty() || size.size() > 20 ||
                !std::all_of(size.begin(),size.end(),[](char c){return c>='0' && c<='9';}) ||
                !lower_sha(hash) || normalized_path(relative) != name ||
                !native_file(name) || mutable_file(name) ||
                !names.insert(name).second || names.size() > max_files)
                throw std::runtime_error("invalid or duplicate GOG receipt entry");
            const auto file = game / relative;
            if (!safe_ancestry(game,relative) || !regular(file) ||
                fs::file_size(file) != std::stoull(size) || sha256_file(file) != hash)
                throw std::runtime_error("GOG file changed or missing: " + name);
        }
        if (in.bad() || !required_immutable(names))
            throw std::runtime_error("incomplete GOG receipt");
        for (const auto name : {"ubi.ini", "dll/snd_cpa.ini"}) {
            const auto relative = fs::u8path(name);
            if (!safe_ancestry(game,relative) || !regular(game / relative))
                throw std::runtime_error("GOG native configuration missing");
        }
        if (!matches_executable(fs::file_size(game / "rayman2.exe"),
                                sha256_file(game / "rayman2.exe")))
            throw std::runtime_error("GOG executable digest not approved");
        problem.clear();
        return true;
    } catch (const std::exception& e) {
        problem = e.what();
        return false;
    }
}
} // namespace r2fix::gog
