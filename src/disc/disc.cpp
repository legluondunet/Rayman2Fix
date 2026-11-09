#include "rayman2fix/disc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

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

std::string decode_name(const unsigned char* p, std::size_t size) {
    if (size == 1 && (p[0] == 0 || p[0] == 1)) return "";
    std::string name(reinterpret_cast<const char*>(p), size);
    const auto semi = name.find(';');
    if (semi != std::string::npos) name.resize(semi);
    while (!name.empty() && name.back() == '.') name.pop_back();
    if (name.empty() || name == "." || name == "..") fail("invalid directory entry name");
    if (name.size() > 120) fail("unreasonably long ISO 9660 filename");
    for (const unsigned char c : name) {
        if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            fail("unsafe or unsupported ISO 9660 filename");
    }
    if (name.back() == ' ') fail("trailing space in disc filename");
    const std::string stem = upper(name.substr(0, name.find('.')));
    const std::set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
        "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    if (reserved.count(stem)) fail("Windows-reserved ISO filename");
    return name;
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
    ancestors.push_back(extent);
    const auto bytes = read_user(extent, 0, size);
    std::set<std::string> names;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto len = bytes[pos];
        if (len == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (len < 34 || len > bytes.size() - pos)
            fail("invalid ISO directory record length");
        const unsigned char* rec = bytes.data() + pos;
        const auto name_length = static_cast<std::size_t>(rec[32]);
        if (33 + name_length > len) fail("invalid ISO name length");
        const auto flags = rec[25];
        if (flags & 0x80) fail("multi-extent ISO files are not implemented");
        const std::string name = decode_name(rec + 33, name_length);
        pos += len;
        if (name.empty()) continue;  // dot/dot-dot
        if (!names.insert(upper(name)).second)
            fail("case-insensitive filename collision on disc");
        const auto child_extent = le32(rec + 2);
        const auto child_size = le32(rec + 10);
        const fs::path relative = parent / name;
        if (flags & 0x02) {
            scan_directory(child_extent, child_size, relative, depth + 1, ancestors);
        } else {
            if (info_.files.size() >= 50000)
                fail("too many files on disc");
            if (info_.total_bytes > std::numeric_limits<std::uint64_t>::max() - child_size)
                fail("file total overflow");
            info_.files.push_back({relative, child_extent, child_size});
            info_.total_bytes += child_size;
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
    image.track_start_ = track.first_sector * track.sector_bytes;
    image.sector_bytes_ = track.sector_bytes;
    image.user_data_offset_ = track.payload_offset;

    const auto pvd = image.read_user(16, 0, 2048);
    if (pvd[0] != 1 || std::string(reinterpret_cast<const char*>(pvd.data() + 1), 5) != "CD001" || pvd[6] != 1)
        fail("ISO 9660 primary volume descriptor not found at sector 16");
    std::string volume(reinterpret_cast<const char*>(pvd.data() + 40), 32);
    volume.erase(std::find(volume.begin(), volume.end(), '\0'), volume.end());
    image.info_.volume_id = trim(volume);
    const auto* root = pvd.data() + 156;
    if (root[0] < 34 || !(root[25] & 0x02))
        fail("invalid ISO 9660 root directory");
    std::vector<std::uint32_t> ancestors;
    image.scan_directory(le32(root + 2), le32(root + 10), fs::path(), 0, ancestors);
    return image;
}

void DiscImage::extract(const fs::path& destination) const {
    if (destination.empty() || destination == "." || destination == "/")
        fail("unsafe extraction destination");
    if (fs::exists(destination)) fail("destination already exists; no files overwritten");
    if (!destination.parent_path().empty())
        fs::create_directories(destination.parent_path());
    const fs::path staging(destination.string() + ".staging");
    if (fs::exists(staging)) fail("staging directory exists; inspect/remove manually");

    fs::create_directory(staging);
    try {
        for (const auto& file : info_.files) {
            const fs::path out = staging / file.relative_path;
            fs::create_directories(out.parent_path());
            std::ofstream dest(out, std::ios::binary | std::ios::trunc);
            if (!dest) fail("cannot create output: " + out.string());
            std::uint64_t pos = 0;
            while (pos < file.size_bytes) {
                const auto n = static_cast<std::size_t>(
                    std::min<std::uint64_t>(file.size_bytes - pos, 2048));
                const auto chunk = read_user(file.extent_lba, pos, n);
                dest.write(reinterpret_cast<const char*>(chunk.data()),
                           static_cast<std::streamsize>(chunk.size()));
                if (!dest) fail("cannot write output: " + out.string());
                pos += n;
            }
            dest.close();
            if (!dest) fail("error closing extracted file: " + out.string());
        }
        fs::rename(staging, destination);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(staging, ec);
        throw;
    }
}

}  // namespace r2fix
