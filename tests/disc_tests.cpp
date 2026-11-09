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

// Joliet supplementary volume descriptors store UCS-2 big-endian paths.
// The primary tree deliberately has only an unrelated 8.3 name.
static std::string joliet_ucs2(const std::u16string& name) {
    std::string result;
    for (char16_t cp : name) {
        result.push_back(static_cast<char>(cp >> 8));
        result.push_back(static_cast<char>(cp & 255));
    }
    return result;
}
static std::vector<std::uint8_t> make_joliet_iso() {
    auto iso = make_iso("OLDNAME.TXT;1");
    iso.resize(32 * 2048);
    put32(iso.data() + 16 * 2048 + 80, 32);
    auto* svd = iso.data() + 17 * 2048;
    svd[0] = 2;
    svd[1] = 'C'; svd[2] = 'D'; svd[3] = '0'; svd[4] = '0';
    svd[5] = '1'; svd[6] = 1;
    svd[88] = '%'; svd[89] = '/'; svd[90] = 'E';
    put32(svd + 80, 32);
    record(svd + 156, 24, 2048, 2, std::string(1, '\0'));
    auto* terminator = iso.data() + 18 * 2048;
    terminator[0] = 255; terminator[1] = 'C'; terminator[2] = 'D';
    terminator[3] = '0'; terminator[4] = '0'; terminator[5] = '1';
    terminator[6] = 1;

    auto* dir = iso.data() + 24 * 2048;
    std::size_t pos = record(dir, 24, 2048, 2, std::string(1, '\0'));
    pos += record(dir + pos, 24, 2048, 2, std::string(1, '\1'));
    record(dir + pos, 26, 2048, 2,
           joliet_ucs2(u"R\u00C9PERTOIRE LONG"));
    auto* sub = iso.data() + 26 * 2048;
    pos = record(sub, 26, 2048, 2, std::string(1, '\0'));
    pos += record(sub + pos, 24, 2048, 2, std::string(1, '\1'));
    record(sub + pos, 27, 6, 0,
           joliet_ucs2(u"TR\u00C8S-LONG-NOM.DAT;1"));
    std::copy_n("HELLO\n", 6, iso.data() + 27 * 2048);
    return iso;
}
// Original retail CD has one DLL tree. The other directory is unrelated.
// Case-variant duplicate directories are a malformed/unexpected CD, not a
// reason to perform the old Ray2Fix/dgVoodoo packaging merge.
static std::vector<std::uint8_t> make_joliet_cd_dirs_iso(
    bool duplicate_case_directory = false, bool duplicate_case_file = false) {
    auto iso = make_joliet_iso();
    iso.resize(34 * 2048);
    put32(iso.data() + 16 * 2048 + 80, 34);
    put32(iso.data() + 17 * 2048 + 80, 34);
    auto* root = iso.data() + 24 * 2048;
    std::fill(root, root + 2048, 0);
    std::size_t pos = record(root, 24, 2048, 2, std::string(1, '\0'));
    pos += record(root + pos, 24, 2048, 2, std::string(1, '\1'));
    pos += record(root + pos, 28, 2048, 2, joliet_ucs2(u"DLL"));
    record(root + pos, 29, 2048, 2,
           joliet_ucs2(duplicate_case_directory ? u"Dll" : u"Assets"));
    for (int i = 0; i < 2; ++i) {
        const int sector = 28 + i;
        auto* dir = iso.data() + sector * 2048;
        std::size_t p = record(dir, sector, 2048, 2, std::string(1, '\0'));
        p += record(dir + p, 24, 2048, 2, std::string(1, '\1'));
        p += record(dir + p, 30 + i, 6, 0,
                    joliet_ucs2(i == 0 ? u"First.DLL;1" : u"SECOND.dll;1"));
        if (duplicate_case_file && i == 0)
            record(dir + p, 31, 6, 0, joliet_ucs2(u"FIRST.dll;1"));
        std::copy_n("HELLO\n", 6, iso.data() + (30 + i) * 2048);
    }
    return iso;
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
        check(parsed.info().filename_source.find("primary") != std::string::npos,
              "short-name fallback is explicitly identified");

        // A Joliet tree must supersede the DOS 8.3 tree. Names are written
        // in lowercase, including Unicode accents and nested directory names.
        const auto unicode_iso = make_joliet_iso();
        write_file(root / "joliet.iso", unicode_iso);
        const auto joliet = r2fix::DiscImage::open(root / "joliet.iso");
        check(joliet.info().filename_source.find("Joliet") != std::string::npos,
              "Joliet long names selected over primary ISO 8.3");
        check(joliet.info().files.size() == 1, "one Joliet long-name file");
        const auto unicode_relative = fs::u8path(u8"r\u00E9pertoire long") /
                                      fs::u8path(u8"tr\u00E8s-long-nom.dat");
        check(joliet.info().files[0].relative_path == unicode_relative,
              "accented directory and long file are lowercase");
        joliet.extract(root / "joliet-extracted");
        const auto actual_file = root / "joliet-extracted" / unicode_relative;
        check(fs::exists(actual_file), "Joliet long file extracted");
        check(!fs::exists(root / "joliet-extracted" / "oldname.txt"),
              "short primary ISO 8.3 name is never extracted when Joliet exists");
        check((fs::status(actual_file).permissions() & fs::perms::owner_write)
                != fs::perms::none,
              "extracted file is writable");
        check((fs::status(actual_file.parent_path()).permissions() & fs::perms::owner_write)
                != fs::perms::none,
              "extracted directory is writable");

        const auto cd_dirs_iso = make_joliet_cd_dirs_iso();
        write_file(root / "joliet-single-dll.iso", cd_dirs_iso);
        const auto cd = r2fix::DiscImage::open(root / "joliet-single-dll.iso");
        check(cd.info().files.size() == 2, "one CD DLL tree and one assets tree");
        cd.extract(root / "joliet-single-dll-extracted");
        check(fs::exists(root / "joliet-single-dll-extracted" / "dll" / "first.dll"),
              "original disc DLL directory extracted in lowercase");
        check(fs::exists(root / "joliet-single-dll-extracted" / "assets" / "second.dll"),
              "unrelated source tree retained");
        unsigned dll_folder_count = 0;
        for (const auto& entry : fs::directory_iterator(root / "joliet-single-dll-extracted")) {
            if (entry.is_directory() && entry.path().filename().u8string() == "dll")
                ++dll_folder_count;
            check(entry.path().filename().u8string() != "DLL",
                  "no actual uppercase DLL directory is created");
        }
        check(dll_folder_count == 1, "one lowercase dll directory in output");

        write_file(root / "unexpected-case-folders.iso",
                   make_joliet_cd_dirs_iso(true));
        check(fails(root / "unexpected-case-folders.iso"),
              "unexpected same-target directories are rejected instead of merged");
        write_file(root / "unexpected-case-files.iso",
                   make_joliet_cd_dirs_iso(false, true));
        check(fails(root / "unexpected-case-files.iso"),
              "unexpected colliding lowercase files cannot overwrite each other");

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
        check(!fs::exists(root / "filtered" / "data" / "world" / "levels" / "map0.dat"),
              "historical excluded file not extracted");
        std::ifstream real(root / "filtered" / "data" / "world" / "levels" / "real.dat",
                           std::ios::binary);
        check(static_cast<bool>(real), "real nested file exists");
        std::string real_text((std::istreambuf_iterator<char>(real)), {});
        check(real_text == "HELLO\n", "real nested file contents preserved");
        real.close();

        // The owner's three root-only SafeDisc auxiliary files are not
        // installed. Other files with the same extension in game directories
        // must still be retained; exclusions are exact paths, not globs.
        auto obsolete_cd = make_rayman2_with_dummy();
        obsolete_cd.resize(31 * 2048);
        put32(obsolete_cd.data() + 16 * 2048 + 80, 31);
        auto* obsolete_root = obsolete_cd.data() + 20 * 2048;
        std::size_t root_pos = record(obsolete_root, 20, 2048, 2, std::string(1, '\0'));
        root_pos += record(obsolete_root + root_pos, 20, 2048, 2, std::string(1, '\1'));
        root_pos += record(obsolete_root + root_pos, 21, 2048, 2, "DATA");
        root_pos += record(obsolete_root + root_pos, 26, 4, 0, "00000001.TMP;1");
        root_pos += record(obsolete_root + root_pos, 27, 4, 0, "0000040C.016;1");
        root_pos += record(obsolete_root + root_pos, 28, 4, 0, "0000040C.256;1");
        auto* data_dir = obsolete_cd.data() + 21 * 2048;
        std::size_t data_pos = record(data_dir, 21, 2048, 2, std::string(1, '\0'));
        data_pos += record(data_dir + data_pos, 20, 2048, 2, std::string(1, '\1'));
        data_pos += record(data_dir + data_pos, 22, 2048, 2, "WORLD");
        record(data_dir + data_pos, 29, 4, 0, "0000040C.016;1");
        std::copy_n("TEST", 4, obsolete_cd.data() + 29 * 2048);
        write_file(root / "obsolete_cd.iso", obsolete_cd);
        const auto obsolete_image = r2fix::DiscImage::open(root / "obsolete_cd.iso");
        check(obsolete_image.info().skipped_files == 4,
              "three obsolete root files plus the known dummy are omitted");
        check(obsolete_image.info().extract_bytes == 10,
              "nested similarly named file remains selected");
        obsolete_image.extract(root / "obsolete-cd-extracted");
        check(!fs::exists(root / "obsolete-cd-extracted" / "00000001.tmp") &&
              !fs::exists(root / "obsolete-cd-extracted" / "0000040c.016") &&
              !fs::exists(root / "obsolete-cd-extracted" / "0000040c.256"),
              "none of the three protection auxiliaries is extracted");
        check(fs::exists(root / "obsolete-cd-extracted" / "data" / "0000040c.016"),
              "same extension and name under data directory are preserved");

        // A foreign disc must not silently inherit RAYMAN2-specific rules.
        auto foreign_aux = obsolete_cd;
        auto* foreign_pvd = foreign_aux.data() + 16 * 2048;
        std::fill(foreign_pvd + 40, foreign_pvd + 72, ' ');
        std::copy_n("OTHER_CD", 8, foreign_pvd + 40);
        write_file(root / "foreign-aux.iso", foreign_aux);
        const auto foreign = r2fix::DiscImage::open(root / "foreign-aux.iso");
        check(foreign.info().skipped_files == 0 && foreign.info().suspect_files == 1,
              "foreign disc receives neither protection auxiliary nor dummy exemptions");

        // 8.3 primary ISO names for the six long GRAPHICS*/TEXTURES*
        // exclusions in the user's Lutris CD installer. One bogus alias had
        // appeared as GRAPHI~3.DAT in a real RAYMAN2 ISO log.
        auto dos_aliases = make_rayman2_with_dummy();
        auto* levels = dos_aliases.data() + 23 * 2048;
        std::fill(levels, levels + 2048, 0);
        std::size_t alias_pos = record(levels, 23, 2048, 2, std::string(1, '\0'));
        alias_pos += record(levels + alias_pos, 22, 2048, 2, std::string(1, '\1'));
        const char* ignored_aliases[] = {
            "GRAPHI~1.DAT;1", "GRAPHI~2.DAT;1", "GRAPHI~3.DAT;1",
            "TEXTUR~1.DAT;1", "TEXTUR~2.DAT;1", "TEXTUR~3.DAT;1"
        };
        for (const char* alias : ignored_aliases)
            alias_pos += record(levels + alias_pos, 200578, 437458886, 0, alias);
        record(levels + alias_pos, 25, 6, 0, "REAL.DAT;1");
        write_file(root / "dos_aliases.iso", dos_aliases);
        const auto aliased = r2fix::DiscImage::open(root / "dos_aliases.iso");
        check(aliased.info().skipped_files == 6, "all six SFN aliases omitted");
        check(aliased.info().suspect_files == 0, "Lutris SFN exclusions are not suspects");
        check(aliased.info().extract_bytes == 6, "only real data selected");
        aliased.extract(root / "aliases-extracted");
        check(fs::exists(root / "aliases-extracted" / "data" / "world" / "levels" / "real.dat"),
              "needed game data preserved alongside short-name exclusions");
        check(!fs::exists(root / "aliases-extracted" / "data" / "world" / "levels" / "graphi~3.dat"),
              "the observed GRAPHI~3.DAT is not extracted");

        auto unrecognized_alias = make_rayman2_with_dummy();
        auto* other_levels = unrecognized_alias.data() + 23 * 2048;
        std::fill(other_levels, other_levels + 2048, 0);
        std::size_t other_pos = record(other_levels, 23, 2048, 2, std::string(1, '\0'));
        other_pos += record(other_levels + other_pos, 22, 2048, 2, std::string(1, '\1'));
        other_pos += record(other_levels + other_pos, 200578, 437458886, 0,
                            "GRAPHI~4.DAT;1");
        record(other_levels + other_pos, 25, 6, 0, "REAL.DAT;1");
        write_file(root / "unknown_alias.iso", unrecognized_alias);
        const auto unknown_alias = r2fix::DiscImage::open(root / "unknown_alias.iso");
        check(unknown_alias.info().suspect_files == 1 &&
              unknown_alias.info().skipped_files == 0,
              "do not widen exact SFN exclusions to unexpected suffixes");

        // The installer removes root setup files and directories as well.
        auto setup_files = make_rayman2_with_dummy();
        auto* setup_root = setup_files.data() + 20 * 2048;
        std::size_t setup_pos = record(setup_root, 20, 2048, 2, std::string(1, '\0'));
        setup_pos += record(setup_root + setup_pos, 20, 2048, 2, std::string(1, '\1'));
        setup_pos += record(setup_root + setup_pos, 21, 2048, 2, "DATA");
        record(setup_root + setup_pos, 200578, 437458886, 0, "SETUP.EXE;1");
        write_file(root / "installer_setup.iso", setup_files);
        const auto setup = r2fix::DiscImage::open(root / "installer_setup.iso");
        check(setup.info().skipped_files == 2, "setup exe and MAP0.DAT skipped");
        check(setup.info().suspect_files == 0, "known installer-only paths are excluded");

        // A root DIRECTX directory is excluded recursively, but unrelated
        // subdirectories are still traversed and validated.
        auto setup_dir = make_rayman2_with_dummy();
        setup_dir.resize(28 * 2048);
        put32(setup_dir.data() + 16 * 2048 + 80, 28);
        auto* dir_root = setup_dir.data() + 20 * 2048;
        std::size_t dir_pos = record(dir_root, 20, 2048, 2, std::string(1, '\0'));
        dir_pos += record(dir_root + dir_pos, 20, 2048, 2, std::string(1, '\1'));
        dir_pos += record(dir_root + dir_pos, 21, 2048, 2, "DATA");
        record(dir_root + dir_pos, 26, 2048, 2, "DIRECTX");
        auto* directx_dir = setup_dir.data() + 26 * 2048;
        std::size_t dx_pos = record(directx_dir, 26, 2048, 2, std::string(1, '\0'));
        dx_pos += record(directx_dir + dx_pos, 20, 2048, 2, std::string(1, '\1'));
        record(directx_dir + dx_pos, 200578, 437458886, 0, "DSETUP.DLL;1");
        write_file(root / "installer_dir.iso", setup_dir);
        const auto dx = r2fix::DiscImage::open(root / "installer_dir.iso");
        check(dx.info().skipped_files == 2 && dx.info().suspect_files == 0,
              "root installer directories skipped recursively");
        dx.extract(root / "installer-dir-extracted");
        check(!fs::exists(root / "installer-dir-extracted" / "directx"),
              "excluded installer-only tree is never copied");

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
        std::ifstream content(target / "data.txt", std::ios::binary);
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
