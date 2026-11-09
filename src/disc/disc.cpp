#include "rayman2fix/disc.hpp"
#include "rayman2fix/cd_exclusions.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <codecvt>
#include <locale>
#include <map>
#include <cwctype>
#include <fstream>
#include <limits>
#include <set>
#include <utility>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace r2fix {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("disc image: " + message);
}

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// The ISO 9660 on-disc 733 number stores both little- and big-endian copies.
// Comparing them detects malformed records before trusting their extents.
std::uint32_t iso733(const unsigned char* p) {
    const auto little = le32(p);
    const auto big = (static_cast<std::uint32_t>(p[4]) << 24) |
                     (static_cast<std::uint32_t>(p[5]) << 16) |
                     (static_cast<std::uint32_t>(p[6]) << 8) |
                      static_cast<std::uint32_t>(p[7]);
    if (little != big) fail("ISO 9660 extent/size endian copies disagree");
    return little;
}

std::string upper(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool ends_with_case_insensitive(const fs::path& path, const char* extension) {
    return upper(path.extension().string()) == extension;
}

// Joliet stores file identifiers as UCS-2 big-endian; the primary
// ISO 9660 tree often exposes only 8.3 DOS aliases. Prefer the Joliet
// supplementary tree when available and never fabricate missing long names.
void append_utf8(std::string& output, std::uint32_t cp) {
    if (cp <= 0x7f) {
        output.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        output.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else if (cp <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        output.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        output.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
}

std::string lowercase_utf8(const std::string& value) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     value.data(), static_cast<int>(value.size()),
                                     nullptr, 0);
    if (!n) fail("invalid UTF-8 ISO filename");
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value.data(), static_cast<int>(value.size()),
                            wide.data(), n) != n)
        fail("cannot convert Joliet filename to Unicode");
    const int needed = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                    wide.data(), n, nullptr, 0, nullptr, nullptr, 0);
    if (!needed) fail("Windows Unicode lowercase conversion unavailable");
    std::wstring lowered(static_cast<std::size_t>(needed), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                      wide.data(), n, lowered.data(), needed,
                      nullptr, nullptr, 0) != needed)
        fail("Windows Unicode lowercase conversion failed");
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         lowered.data(), needed,
                                         nullptr, 0, nullptr, nullptr);
    if (!bytes) fail("cannot encode lowercase Unicode filename");
    std::string out(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            lowered.data(), needed, out.data(), bytes,
                            nullptr, nullptr) != bytes)
        fail("cannot encode lowercase Unicode filename");
    return out;
#else
    std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8;
    std::wstring w;
    try {
        w = utf8.from_bytes(value);
    } catch (const std::range_error&) {
        fail("invalid UTF-8 ISO filename");
    }
    static const std::locale unicode_locale = [] {
        try { return std::locale("C.UTF-8"); }
        catch (const std::runtime_error&) { return std::locale::classic(); }
    }();
    for (auto& cp : w) {
        cp = std::tolower(cp, unicode_locale);
        // Consistent basic Latin and accented Latin-1 fallback if a host lacks
        // a UTF-8 locale; on Windows the native Unicode mapping is used.
        if (cp >= L'A' && cp <= L'Z') cp += L'a' - L'A';
        else if (cp >= 0x00c0 && cp <= 0x00d6) cp += 0x20;
        else if (cp >= 0x00d8 && cp <= 0x00de) cp += 0x20;
    }
    return utf8.to_bytes(w);
#endif
}

std::string decode_name(const unsigned char* p, std::size_t size, bool joliet) {
    if (size == 1 && (p[0] == 0 || p[0] == 1)) return "";
    std::string name;
    if (joliet) {
        if (size % 2 != 0) fail("odd-length Joliet UCS-2 filename");
        for (std::size_t i = 0; i < size; i += 2) {
            const auto cp = (static_cast<std::uint32_t>(p[i]) << 8) | p[i + 1];
            if (cp >= 0xd800 && cp <= 0xdfff)
                fail("unsupported surrogate in Joliet UCS-2 filename");
            append_utf8(name, cp);
        }
    } else {
        name.assign(reinterpret_cast<const char*>(p), size);
        for (const unsigned char c : name)
            if (c >= 127) fail("non-ASCII filename in ISO 9660 primary directory");
    }
    // Strip the ISO 9660 version suffix (usually ;1), but not an ordinary
    // embedded semicolon in an actual long filename.
    const auto semi = name.rfind(';');
    if (semi != std::string::npos && semi + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(semi + 1),
                    name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        name.resize(semi);
    while (!name.empty() && name.back() == '.') name.pop_back();
    if (name.empty() || name == "." || name == "..")
        fail("invalid directory entry name");
    if (name.size() > 240) fail("unreasonably long ISO filename");
    for (const unsigned char c : name)
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*' || c == '\0')
            fail("unsafe ISO filename");
    if (name.back() == ' ') fail("trailing space in ISO filename");

    name = lowercase_utf8(name);
    // Windows device names remain reserved even with extensions.
    const std::string stem = upper(name.substr(0, name.find('.')));
    const std::set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
        "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    if (reserved.count(stem)) fail("Windows-reserved ISO filename");
    return name;
}

// Exact removals performed by the user's existing CD Lutris installer:
// https://lutris.net/games/install/39141/view
// Their presence in that recipe is evidence that installation did not require
// copying them, NOT proof that each is a dummy or physically invalid file.
bool ignored_by_rayman2_cd_recipe(const std::string& volume_id,
                                  const fs::path& relative) {
    if (upper(volume_id) != "RAYMAN2") return false;
    const std::string path = upper(relative.generic_u8string());
    // Explicit root-only CD exclusions requested by the user. Do not
    // broaden these paths to nested files or other extension matches.
    if (excluded_cd_root_auxiliary(relative))
        return true;
    constexpr const char* ignored[] = {
        "DATA/WORLD/LEVELS/GRAPHICS2.DAT",
        "DATA/WORLD/LEVELS/MAP1.DAT",
        "DATA/WORLD/LEVELS/ANIMS1.DAT",
        "DATA/WORLD/LEVELS/MUSIC2.DAT",
        "DATA/WORLD/LEVELS/MUSIC1.DAT",
        "DATA/WORLD/LEVELS/SOUNDS0.DAT",
        "DATA/WORLD/LEVELS/GRAPHICS0.DAT",
        "DATA/WORLD/LEVELS/SOUNDS1.DAT",
        "DATA/WORLD/LEVELS/LEVELS1.DAT",
        "DATA/WORLD/LEVELS/ANIMS0.DAT",
        "DATA/WORLD/LEVELS/MAP2.DAT",
        "DATA/WORLD/LEVELS/SOUNDS2.DAT",
        "DATA/WORLD/LEVELS/TEXTURES2.DAT",
        "DATA/WORLD/LEVELS/ANIMS2.DAT",
        "DATA/WORLD/LEVELS/MENU2.DAT",
        "DATA/WORLD/LEVELS/MENU0.DAT",
        "DATA/WORLD/LEVELS/MUSIC0.DAT",
        "DATA/WORLD/LEVELS/TEXTURES0.DAT",
        "DATA/WORLD/LEVELS/TEXTURES1.DAT",
        "DATA/WORLD/LEVELS/LEVELS2.DAT",
        "DATA/WORLD/LEVELS/MAP0.DAT",
        "DATA/WORLD/LEVELS/MENU1.DAT",
        "DATA/WORLD/LEVELS/GRAPHICS1.DAT"
    };
    for (const char* item : ignored)
        if (path == item) return true;

    // The retail ISO9660 primary directory can contain DOS 8.3 aliases
    // instead of the long filenames used in Lutris' mounted-CD cleanup:
    // GRAPHICS0/1/2.DAT -> GRAPHI~1/2/3.DAT
    // TEXTURES0/1/2.DAT -> TEXTUR~1/2/3.DAT
    // We cannot prove which suffix maps to which digit without Joliet data,
    // but all six original names are explicit Lutris exclusions. Restrict
    // aliases to precisely these two stems and three numeric suffixes.
    constexpr const char* levels = "DATA/WORLD/LEVELS/";
    if (path.rfind(levels, 0) == 0) {
        const auto name = path.substr(std::char_traits<char>::length(levels));
        if (name.size() == 12 && name[6] == '~' &&
            name[7] >= '1' && name[7] <= '3' &&
            name.substr(8) == ".DAT" &&
            (name.substr(0, 6) == "GRAPHI" ||
             name.substr(0, 6) == "TEXTUR"))
            return true;
    }

    // The original CD contains one DLL directory. The legacy Lutris
    // DLL/Dll consolidation existed solely because Ray2Fix and dgVoodoo2
    // introduced additional files with inconsistent path casing. We do not
    // import those components and therefore do not merge DLL directories.
    constexpr const char* excluded_dirs[] = {
        "ACROBAT", "DIRECTX", "DRIVERS", "INSTDATA", "PADS",
        "TONIC TROUBLE", "TONICT~1"
    };
    const auto slash = path.find('/');
    const auto root_name = path.substr(0, slash);
    for (const char* folder : excluded_dirs)
        if (root_name == folder && slash != std::string::npos)
            return true;

    if (slash == std::string::npos) {
        constexpr const char* excluded_root_files[] = {
            "AUTORUN.INF", "D3DIMM.DLL", "DDRAW.DLL",
            "GXSETUP.EXE", "SETUP.EXE", "SETUPUBI.EXE"
        };
        for (const char* item : excluded_root_files)
            if (path == item) return true;
        if (path.rfind("DSETUP", 0) == 0) return true; // Lutris DSETUP* glob
    }
    return false;
}

struct TrackSpec {
    fs::path file;
    std::uint64_t first_sector = 0;
    std::uint32_t sector_bytes = 2048;
    std::uint32_t payload_offset = 0;
};

std::uint64_t parse_msf(const std::string& value) {
    unsigned m = 0, s = 0, f = 0;
    char colon1 = 0, colon2 = 0;
    std::istringstream in(value);
    if (!(in >> m >> colon1 >> s >> colon2 >> f) ||
        colon1 != ':' || colon2 != ':' || s >= 60 || f >= 75)
        fail("invalid CUE INDEX time");
    std::string extra;
    if (in >> extra) fail("invalid CUE INDEX trailing content");
    return (static_cast<std::uint64_t>(m) * 60 + s) * 75 + f;
}

TrackSpec parse_cue(const fs::path& cue) {
    std::ifstream input(cue);
    if (!input) fail("cannot open CUE file");

    fs::path current_file;
    TrackSpec selected;
    bool have_selected = false;
    bool data_track = false;
    bool selected_index = false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        tag = upper(tag);

        if (tag == "FILE") {
            const auto left = line.find('"');
            const auto right = left == std::string::npos
                                   ? std::string::npos : line.find('"', left + 1);
            if (left == std::string::npos || right == std::string::npos)
                fail("CUE FILE must use a quoted path");
            const fs::path filename(line.substr(left + 1, right - left - 1));
            if (filename.empty() || filename.is_absolute()) fail("invalid CUE file path");
            for (const auto& component : filename)
                if (component == "..") fail("CUE path traversal refused");
            const auto file_type = upper(trim(line.substr(right + 1)));
            if (file_type != "BINARY") fail("only BINARY CUE tracks are supported");
            current_file = cue.parent_path() / filename;
        } else if (tag == "TRACK") {
            std::string number, mode;
            in >> number >> mode;
            if (current_file.empty()) fail("CUE TRACK appears before FILE");
            mode = upper(mode);
            data_track = mode != "AUDIO";
            if (data_track) {
                if (mode != "MODE1/2352" && mode != "MODE1/2048")
                    fail("unsupported data-track sector layout: " + mode);
                if (have_selected) fail("multiple CUE data tracks are unsupported");
                have_selected = true;
                selected.file = current_file;
                selected.sector_bytes = mode == "MODE1/2352" ? 2352 : 2048;
                selected.payload_offset = mode == "MODE1/2352" ? 16 : 0;
                selected_index = false;
            }
        } else if (tag == "INDEX" && data_track) {
            std::string index, timestamp;
            in >> index >> timestamp;
            if (index == "01") {
                selected.first_sector = parse_msf(timestamp);
                selected_index = true;
            }
        }
    }
    if (!have_selected || !selected_index)
        fail("CUE requires one MODE1 data track with INDEX 01");
    if (!fs::is_regular_file(selected.file))
        fail("CUE referenced BIN file is missing");
    return selected;
}

}  // namespace

void DiscImage::validate_extent(std::uint32_t extent, std::uint32_t size) const {
    const auto end = static_cast<std::uint64_t>(extent) +
                     (static_cast<std::uint64_t>(size) + 2047) / 2048;
    const auto volume_sectors = info_.volume_bytes / 2048;
    const auto physical_sectors = image_bytes_ >= track_start_
        ? (image_bytes_ - track_start_) / sector_bytes_ : 0;
    if (end > volume_sectors || end > physical_sectors)
        fail("file/directory extent outside ISO volume or physical image: LBA=" +
             std::to_string(extent) + " bytes=" + std::to_string(size) +
             " volume_sectors=" + std::to_string(volume_sectors) +
             " image_sectors=" + std::to_string(physical_sectors));
}

std::vector<unsigned char> DiscImage::read_user(
    std::uint64_t lba, std::uint64_t offset, std::size_t length) const {
    std::vector<unsigned char> out(length);
    std::ifstream in(data_file_, std::ios::binary);
    if (!in) fail("cannot read image data file");
    std::size_t done = 0;
    while (done < length) {
        const std::uint64_t pos = offset + done;
        const std::uint64_t block = lba + pos / 2048;
        const std::uint64_t within = pos % 2048;
        const std::size_t count = std::min<std::size_t>(
            2048 - static_cast<std::size_t>(within), length - done);
        const std::uint64_t absolute =
            track_start_ + block * sector_bytes_ + user_data_offset_ + within;
        if (absolute > image_bytes_ || count > image_bytes_ - absolute ||
            absolute > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
            fail("extent exceeds data track or image is truncated");
        in.seekg(static_cast<std::streamoff>(absolute));
        in.read(reinterpret_cast<char*>(out.data() + done),
                static_cast<std::streamsize>(count));
        if (!in) fail("short read from image");
        done += count;
    }
    return out;
}

void DiscImage::scan_directory(std::uint32_t extent,
                                std::uint32_t size,
                                const fs::path& parent,
                                unsigned depth,
                                std::vector<std::uint32_t>& ancestors) {
    if (depth > 32 || size > 32 * 1024 * 1024)
        fail("excessive directory depth or directory length");
    if (std::find(ancestors.begin(), ancestors.end(), extent) != ancestors.end())
        fail("recursive ISO directory extent");
    validate_extent(extent, size);
    ancestors.push_back(extent);
    const auto bytes = read_user(extent, 0, size);
    std::set<std::string> names; // Reject ambiguous lowercase output paths
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto len = bytes[pos];
        if (len == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (len < 34 || len > bytes.size() - pos ||
            (pos % 2048) + len > 2048)
            fail("invalid ISO directory record length");
        const unsigned char* rec = bytes.data() + pos;
        const auto name_length = static_cast<std::size_t>(rec[32]);
        if (33 + name_length > len) fail("invalid ISO name length");
        const auto flags = rec[25];
        if (flags & 0x80) fail("multi-extent ISO files are not implemented");
        if (rec[26] || rec[27]) fail("interleaved ISO file extents are unsupported");
        const std::string name = decode_name(rec + 33, name_length, joliet_);
        pos += len;
        if (name.empty()) continue;  // dot/dot-dot
        const bool directory = (flags & 0x02) != 0;
        if (!names.insert(name).second)
            fail("filename collision after lowercase conversion: " +
                 (parent / fs::u8path(name)).generic_u8string());
        const fs::path relative = parent / fs::u8path(name);
        // An exact historical exclusion may be unreadable; do not generalize
        // that rule to other files just because their declared sizes are huge.
        const bool skipped = !directory &&
            ignored_by_rayman2_cd_recipe(info_.volume_id, relative);
        const auto child_extent = le32(rec + 2);
        const auto child_size = le32(rec + 10);
        if (directory) {
            // Broken directories cannot be catalogued safely, so remain fatal.
            (void)iso733(rec + 2);
            (void)iso733(rec + 10);
            validate_extent(child_extent, child_size);
            scan_directory(child_extent, child_size, relative, depth + 1, ancestors);
        } else {
            if (info_.files.size() >= 50000)
                fail("too many files on disc");
            if (info_.total_bytes > std::numeric_limits<std::uint64_t>::max() - child_size)
                fail("file total overflow");
            std::string issue;
            if (!skipped) {
                try {
                    (void)iso733(rec + 2);
                    (void)iso733(rec + 10);
                    validate_extent(child_extent, child_size);
                } catch (const std::runtime_error& error) {
                    // --inspect must expose the path of the suspect file. The
                    // unsafe extent MUST NOT be read or copied by --extract.
                    issue = error.what();
                }
            }
            if (!skipped && !output_file_paths_.insert(relative).second)
                fail("duplicate lowercase output file: " +
                     relative.generic_u8string());
            info_.files.push_back({relative, child_extent, child_size,
                                   skipped, !issue.empty(), issue});
            info_.total_bytes += child_size;
            if (skipped) {
                info_.skipped_bytes += child_size;
                ++info_.skipped_files;
            } else if (!issue.empty()) {
                ++info_.suspect_files;
            } else {
                info_.extract_bytes += child_size;
            }
        }
    }
    ancestors.pop_back();
}

DiscImage DiscImage::open(const fs::path& source) {
    if (!fs::is_regular_file(source)) fail("source must be an existing ISO or CUE file");
    DiscImage image;
    TrackSpec track;
    if (ends_with_case_insensitive(source, ".ISO")) {
        track.file = source;
        image.info_.format = "ISO9660 / 2048-byte sectors";
    } else if (ends_with_case_insensitive(source, ".CUE")) {
        track = parse_cue(source);
        image.info_.format = "CUE/BIN MODE1 / " + std::to_string(track.sector_bytes);
    } else {
        fail("supported inputs: .iso and .cue (not raw .bin without CUE)");
    }

    image.data_file_ = track.file;
    image.image_bytes_ = fs::file_size(track.file);
    image.info_.image_bytes = image.image_bytes_;
    image.track_start_ = track.first_sector * track.sector_bytes;
    if (image.track_start_ >= image.image_bytes_)
        fail("data track starts outside the image");
    image.sector_bytes_ = track.sector_bytes;
    image.user_data_offset_ = track.payload_offset;

    // ISO 9660: type 1 is the primary volume descriptor (possibly only
    // 8.3 filenames); type 2 may be a Joliet supplementary descriptor.
    // Search all consecutive descriptors, NOT just the first primary one.
    std::vector<unsigned char> pvd, joliet;
    for (std::uint64_t sector = 16; sector < 48; ++sector) {
        const auto descriptor = image.read_user(sector, 0, 2048);
        const bool signature =
            std::string(reinterpret_cast<const char*>(descriptor.data() + 1), 5)
                == "CD001" && descriptor[6] == 1;
        if (!signature) {
            if (pvd.empty()) fail("invalid ISO 9660 volume descriptor");
            break; // Permits minimal synthetic images lacking a terminator.
        }
        if (descriptor[0] == 1 && pvd.empty())
            pvd = descriptor;
        if (descriptor[0] == 2 && joliet.empty() &&
            descriptor[88] == '%' && descriptor[89] == '/' &&
            (descriptor[90] == '@' || descriptor[90] == 'C' || descriptor[90] == 'E'))
            joliet = descriptor;
        if (descriptor[0] == 255) break;
    }
    if (pvd.empty()) fail("ISO 9660 primary volume descriptor not found");

    // The volume label is identified from the primary descriptor for the
    // installed-edition filter; the file/directory root comes from Joliet.
    std::string volume(reinterpret_cast<const char*>(pvd.data() + 40), 32);
    volume.erase(std::find(volume.begin(), volume.end(), '\0'), volume.end());
    image.info_.volume_id = trim(volume);

    image.joliet_ = !joliet.empty();
    image.info_.filename_source = image.joliet_ ?
        "Joliet (UCS-2 long filenames)" :
        "ISO9660 primary (long filenames unavailable; 8.3 aliases possible)";
    const auto& chosen = image.joliet_ ? joliet : pvd;
    const auto volume_sectors = iso733(chosen.data() + 80);
    if (volume_sectors < 17)
        fail("invalid ISO 9660 volume space size");
    image.info_.volume_bytes = static_cast<std::uint64_t>(volume_sectors) * 2048;
    const auto* root = chosen.data() + 156;
    if (root[0] < 34 || !(root[25] & 0x02))
        fail("invalid ISO 9660 root directory");
    std::vector<std::uint32_t> ancestors;
    image.scan_directory(iso733(root + 2), iso733(root + 10), fs::path(), 0, ancestors);

    std::set<std::pair<std::uint32_t, std::uint32_t>> extents;
    for (const auto& file : image.info_.files) {
        if (file.skipped_by_cd_recipe || file.invalid_extent) continue;
        if (extents.insert({file.extent_lba, file.size_bytes}).second)
            image.info_.distinct_extent_bytes += file.size_bytes;
        else ++image.info_.shared_extent_references;
    }
    return image;
}

namespace {

// CD files can be flagged read-only. We do not carry those attributes into
// an installation: all output files and directories must be user-writable.
void make_writable(const fs::path& p) {
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        fail("cannot query extracted file attributes");
    if ((attrs & FILE_ATTRIBUTE_READONLY) &&
        !SetFileAttributesW(p.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY))
        fail("cannot remove Windows read-only attribute");
#endif
    std::error_code error;
    fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, error);
    if (error) fail("cannot mark extracted file/directory writable: " + error.message());
}

} // namespace

ExtractionResult DiscImage::extract(
    const fs::path& destination, const ExtractionCallback& progress) const {
    if (destination.empty() || destination == "." || destination == "/")
        fail("unsafe extraction destination");
    // Abort before touching disk: --inspect is tolerant but --extract is not.
    for (const auto& file : info_.files) {
        if (file.invalid_extent && !file.skipped_by_cd_recipe)
            fail("extraction refused: unrecognized suspect file " +
                 file.relative_path.generic_string() + ": " + file.invalid_reason);
    }
    if (fs::exists(destination)) fail("destination already exists; no files overwritten");
    if (!destination.parent_path().empty())
        fs::create_directories(destination.parent_path());
    fs::path staging = destination;
    staging += ".staging";
    if (fs::exists(staging)) fail("staging directory exists; inspect/remove manually");

    ExtractionResult result{};
    result.skipped_files = info_.skipped_files;
    result.skipped_bytes = info_.skipped_bytes;
    const auto expected_files = info_.files.size() - info_.skipped_files;
    auto notify = [&](const fs::path& current) {
        if (progress) progress(ExtractionProgress{
            result.copied_files, expected_files,
            result.copied_bytes, info_.extract_bytes, current});
    };

    fs::create_directory(staging);
    try {
        make_writable(staging);
        notify({});
        for (const auto& file : info_.files) {
            if (file.skipped_by_cd_recipe) continue;
            const fs::path out = staging / file.relative_path;
            fs::create_directories(out.parent_path());
            make_writable(out.parent_path());
            std::ofstream dest(out, std::ios::binary | std::ios::trunc);
            if (!dest) fail("cannot create output: " + out.u8string());
            std::uint64_t pos = 0;
            std::uint64_t since_notify = 0;
            while (pos < file.size_bytes) {
                const auto n = static_cast<std::size_t>(
                    std::min<std::uint64_t>(file.size_bytes - pos, 2048));
                const auto chunk = read_user(file.extent_lba, pos, n);
                dest.write(reinterpret_cast<const char*>(chunk.data()),
                           static_cast<std::streamsize>(chunk.size()));
                if (!dest) fail("cannot write output: " + out.u8string());
                pos += n;
                since_notify += n;
                if (since_notify >= 1024u * 1024u) {
                    // Include bytes written to the in-flight file. The output
                    // is not reported as finished until the file is closed.
                    if (progress) progress(ExtractionProgress{
                        result.copied_files, expected_files,
                        result.copied_bytes + pos, info_.extract_bytes,
                        file.relative_path});
                    since_notify = 0;
                }
            }
            dest.close();
            if (!dest) fail("error closing extracted file: " + out.u8string());
            make_writable(out);
            ++result.copied_files;
            result.copied_bytes += file.size_bytes;
            notify(file.relative_path);
        }
        // Never announce a successful extraction before the staging directory
        // is safely promoted to the final destination.
        fs::rename(staging, destination);
        return result;
    } catch (...) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        throw;
    }
}

}  // namespace r2fix
