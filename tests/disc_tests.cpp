#include "rayman2fix/disc.hpp"

#include <algorithm>
#include <array>
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

static void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
static void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    for (int i = 0; i < 4; ++i) p[4 + i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
}
static std::size_t record(std::uint8_t* p, std::uint32_t extent,
                          std::uint32_t size, std::uint8_t flags,
                          const std::string& filename) {
    const auto n = filename.size();
    const auto length = 33 + n + (n % 2 == 0 ? 1 : 0);
    p[0] = static_cast<std::uint8_t>(length);
    put32(p + 2, extent);
    put32(p + 10, size);
    p[25] = flags;
    p[28] = 1;
    p[31] = 1;
    p[32] = static_cast<std::uint8_t>(n);
    for (std::size_t i = 0; i < n; ++i) p[33 + i] = static_cast<std::uint8_t>(filename[i]);
    return length;
}
static std::vector<std::uint8_t> make_iso(const std::string& name = "DATA.TXT;1") {
    std::vector<std::uint8_t> iso(22 * 2048, 0);
    auto* pvd = iso.data() + 16 * 2048;
    pvd[0] = 1; pvd[1] = 'C'; pvd[2] = 'D'; pvd[3] = '0';
    pvd[4] = '0'; pvd[5] = '1'; pvd[6] = 1;
    put32(pvd + 80, 22); // ISO volume logical sector count (both byte orders)
    const char* volume = "RAYMAN2_TEST";
    for (unsigned i = 0; volume[i] != 0; ++i) pvd[40 + i] = volume[i];
    record(pvd + 156, 20, 2048, 2, std::string(1, '\0'));
    auto* dir = iso.data() + 20 * 2048;
    std::size_t pos = record(dir, 20, 2048, 2, std::string(1, '\0'));
    pos += record(dir + pos, 20, 2048, 2, std::string(1, '\1'));
    pos += record(dir + pos, 21, 6, 0, name);
    (void)pos;
    auto* file = iso.data() + 21 * 2048;
    for (unsigned i = 0; i < 6; ++i) file[i] = "HELLO\n"[i];
    return iso;
}
// Small synthetic RAYMAN2 image with a deliberately impossible "dummy"
/// under the exact path ignored by the user's historical Lutris CD recipe.
static std::vector<std::uint8_t> make_rayman2_with_dummy(bool recognized_volume = true,
                                                          bool recognized_path = true) {
    auto base = make_iso();
    base.resize(26 * 2048);
    auto* pvd = base.data() + 16 * 2048;
    put32(pvd + 80, 26);
    std::fill(pvd + 40, pvd + 72, ' ');
    const std::string volume = recognized_volume ? "RAYMAN2" : "OTHER_CD";
    std::copy(volume.begin(), volume.end(), pvd + 40);
    auto dir = [&](int sector, int child_sector, const char* child_name) {
        auto* data = base.data() + sector * 2048;
        std::fill(data, data + 2048, 0);
        std::size_t pos = record(data, sector, 2048, 2, std::string(1, '\0'));
        pos += record(data + pos, sector == 20 ? sector : sector - 1,
                      2048, 2, std::string(1, '\1'));
        record(data + pos, child_sector, 2048, 2, child_name);
    };
    dir(20, 21, "DATA");
    dir(21, 22, "WORLD");
    dir(22, 23, "LEVELS");
    auto* levels = base.data() + 23 * 2048;
    std::size_t pos = record(levels, 23, 2048, 2, std::string(1, '\0'));
    pos += record(levels + pos, 22, 2048, 2, std::string(1, '\1'));
    pos += record(levels + pos, 24, 900000000, 0,
                  recognized_path ? "MAP0.DAT;1" : "IMPORTANT.DAT;1");
    record(levels + pos, 25, 6, 0, "REAL.DAT;1");
    std::copy_n("HELLO\n", 6, base.data() + 25 * 2048);
    return base;
}

static void write_file(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    check(static_cast<bool>(out), "create test file");
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(out), "write test file");
}
static bool fails(const fs::path& path) {
    try { (void)r2fix::DiscImage::open(path); }
    catch (const std::exception&) { return true; }
    return false;
}

int main() {
    const auto marker = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() /
                      ("rayman2fix-disc-tests-" + std::to_string(marker));
    fs::create_directories(root);
    try {
        const auto iso = make_iso();
        const auto iso_path = root / "test.iso";
        write_file(iso_path, iso);
        const auto parsed = r2fix::DiscImage::open(iso_path);
        check(parsed.info().files.size() == 1, "ISO file count");
        check(parsed.info().volume_id == "RAYMAN2_TEST", "ISO volume");
        check(parsed.info().total_bytes == 6, "file size");
        check(parsed.info().image_bytes == iso.size(), "physical image size");
        check(parsed.info().volume_bytes == iso.size(), "ISO logical volume size");
        check(parsed.info().distinct_extent_bytes == 6, "unique file extents");

        // A valid ISO may reuse the same physical extent for different filenames.
        // In that case sum of declared file lengths can exceed the image size.
        auto shared = make_iso();
        auto* shared_dir = shared.data() + 20 * 2048;
        std::size_t extra = record(shared_dir, 20, 2048, 2, std::string(1, '\0'));
        extra += record(shared_dir + extra, 20, 2048, 2, std::string(1, '\1'));
        extra += record(shared_dir + extra, 21, 6, 0, "DATA.TXT;1");
        extra += record(shared_dir + extra, 21, 6, 0, "ALIAS.TXT;1");
        (void)extra;
        write_file(root / "shared.iso", shared);
        const auto aliases = r2fix::DiscImage::open(root / "shared.iso");
        check(aliases.info().files.size() == 2, "shared extent file count");
        check(aliases.info().total_bytes == 12, "logical size counts both names");
        check(aliases.info().distinct_extent_bytes == 6, "physical extent counted once");
        check(aliases.info().shared_extent_references == 1, "shared extent reported");

        // Valid ISO records can also be malformed or lie outside the image.
        auto outside = make_iso();
        auto* outside_dir = outside.data() + 20 * 2048;
        std::size_t offset = record(outside_dir, 20, 2048, 2, std::string(1, '\0'));
        offset += record(outside_dir + offset, 20, 2048, 2, std::string(1, '\1'));
        record(outside_dir + offset, 25, 6, 0, "DATA.TXT;1");
        write_file(root / "outside.iso", outside);
        const auto outside_scan = r2fix::DiscImage::open(root / "outside.iso");
        check(outside_scan.info().suspect_files == 1,
              "inspect reports unknown out-of-range file without aborting");
        check(outside_scan.info().files[0].invalid_extent,
              "invalid extent marked and retained in inventory");
        bool blocked_outside = false;
        try { outside_scan.extract(root / "blocked-outside"); }
        catch (const std::exception&) { blocked_outside = true; }
        check(blocked_outside, "unknown extent blocks extraction");
        check(!fs::exists(root / "blocked-outside"),
              "unknown extent fails before creating a destination");

        // Real historical CD copies can list impossible huge dummy entries.
        // Ignore ONLY the exact paths from the original Rayman 2 Lutris recipe.
        const auto r2iso = make_rayman2_with_dummy();
        write_file(root / "rayman2_dummy.iso", r2iso);
        const auto r2 = r2fix::DiscImage::open(root / "rayman2_dummy.iso");
        check(r2.info().skipped_files == 1, "one exact historical CD exclusion");
        check(r2.info().skipped_bytes == 900000000, "huge declared ignored size");
        check(r2.info().total_bytes == 900000006, "full declared bytes");
        check(r2.info().extract_bytes == 6, "actual selected extract size");
        check(r2.info().files.size() == 2, "full directory inventory retained");
        check(r2.info().files[0].skipped_by_cd_recipe, "explicitly marked ignored file");
        r2.extract(root / "filtered");
        check(!fs::exists(root / "filtered" / "DATA" / "WORLD" / "LEVELS" / "MAP0.DAT"),
              "historical excluded file not extracted");
        std::ifstream real(root / "filtered" / "DATA" / "WORLD" / "LEVELS" / "REAL.DAT",
                           std::ios::binary);
        check(static_cast<bool>(real), "real nested file exists");
        std::string real_text((std::istreambuf_iterator<char>(real)), {});
        check(real_text == "HELLO\n", "real nested file contents preserved");
        real.close();

        write_file(root / "not_rayman2.iso", make_rayman2_with_dummy(false, true));
        const auto unrelated = r2fix::DiscImage::open(root / "not_rayman2.iso");
        check(unrelated.info().suspect_files == 1 && unrelated.info().skipped_files == 0,
              "unknown disc is inspected but never allowlisted");
        bool blocked_unrelated = false;
        try { unrelated.extract(root / "blocked-unrelated"); }
        catch (const std::exception&) { blocked_unrelated = true; }
        check(blocked_unrelated && !fs::exists(root / "blocked-unrelated"),
              "unknown disc cannot extract invalid files");
        write_file(root / "not_allowlisted.iso", make_rayman2_with_dummy(true, false));
        const auto unlisted = r2fix::DiscImage::open(root / "not_allowlisted.iso");
        check(unlisted.info().suspect_files == 1 && unlisted.info().skipped_files == 0,
              "unlisted bogus file flagged without automatic omission");
        bool blocked_unlisted = false;
        try { unlisted.extract(root / "blocked-unlisted"); }
        catch (const std::exception&) { blocked_unlisted = true; }
        check(blocked_unlisted && !fs::exists(root / "blocked-unlisted"),
              "unlisted bogus files cannot be silently extracted");

        // Regression: user's CD reported LBA=200578 size=437458886.
        auto real_case = make_iso("UNKNOWN.DAT;1");
        auto* real_dir = real_case.data() + 20 * 2048;
        const auto first = record(real_dir, 20, 2048, 2, std::string(1, '\0'));
        const auto second = record(real_dir + first, 20, 2048, 2, std::string(1, '\1'));
        record(real_dir + first + second, 200578, 437458886, 0, "UNKNOWN.DAT;1");
        write_file(root / "retail_anomaly.iso", real_case);
        const auto problem = r2fix::DiscImage::open(root / "retail_anomaly.iso");
        check(problem.info().suspect_files == 1,
              "retail-size anomaly must not stop --inspect");
        check(problem.info().files[0].extent_lba == 200578 &&
              problem.info().files[0].size_bytes == 437458886,
              "retain offending LBA and file size for logging");
        bool blocked_problem = false;
        try { problem.extract(root / "blocked-retail"); }
        catch (const std::exception&) { blocked_problem = true; }
        check(blocked_problem && !fs::exists(root / "blocked-retail"),
              "retail-size anomaly cannot be extracted without an explicit exception");

        // A valid ISO 9660 disc may contain a boot record before its PVD.
        auto boot_iso = make_iso();
        std::copy(boot_iso.begin() + 16 * 2048, boot_iso.begin() + 17 * 2048,
                  boot_iso.begin() + 17 * 2048);
        boot_iso[16 * 2048] = 0;  // ISO boot record descriptor
        write_file(root / "boot.iso", boot_iso);
        check(r2fix::DiscImage::open(root / "boot.iso").info().files.size() == 1,
              "PVD after boot descriptor");

        const auto target = root / "Rayman2Fix" / "DiscContent";
        parsed.extract(target);
        std::ifstream content(target / "DATA.TXT", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(content)), {});
        check(text == "HELLO\n", "extraction data");
        content.close();  // Windows cannot delete an open output file.
        bool refused = false;
        try { parsed.extract(target); }
        catch (const std::exception&) { refused = true; }
        check(refused, "existing directory must be refused");

        std::vector<std::uint8_t> bin(22 * 2352, 0);
        for (std::size_t i = 0; i < 22; ++i) {
            for (std::size_t j = 0; j < 2048; ++j)
                bin[i * 2352 + 16 + j] = iso[i * 2048 + j];
        }
        write_file(root / "test.bin", bin);
        {
            std::ofstream cue(root / "test.cue");
            cue << "FILE \"test.bin\" BINARY\n"
                << "  TRACK 01 MODE1/2352\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    INDEX 01 00:10:00\n";
        }
        const auto cue = r2fix::DiscImage::open(root / "test.cue");
        check(cue.info().files.size() == 1, "CUE file count");
        check(cue.info().format.find("2352") != std::string::npos, "raw sectors");

        auto bad = make_iso("..;1");
        write_file(root / "unsafe.iso", bad);
        check(fails(root / "unsafe.iso"), "traversal must be refused");
        bad = make_iso();
        bad[16 * 2048 + 1] = 'X';
        write_file(root / "invalid.iso", bad);
        check(fails(root / "invalid.iso"), "invalid ISO descriptor must fail");

        {
            std::ofstream cuefile(root / "unsafe.cue");
            cuefile << "FILE \"../test.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n";
        }
        check(fails(root / "unsafe.cue"), "unsafe CUE path must fail");
        fs::remove_all(root);
        std::cout << "All Rayman2Fix disc parser tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
