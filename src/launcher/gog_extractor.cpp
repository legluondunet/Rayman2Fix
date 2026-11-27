#include "rayman2fix/gog_extractor.hpp"
#include "rayman2fix/sha256.hpp"
#include <fstream>
#include <iterator>
#include <limits>
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
constexpr std::uintmax_t capture_limit = 16u * 1024u * 1024u;
#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct AttributeList {
    std::vector<unsigned char> buffer;
    PPROC_THREAD_ATTRIBUTE_LIST ptr = nullptr;
    AttributeList() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) throw std::runtime_error("cannot size extractor attribute list");
        buffer.resize(bytes);
        auto candidate = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        if (!InitializeProcThreadAttributeList(candidate, 1, 0, &bytes))
            throw std::runtime_error("cannot initialize extractor attribute list");
        ptr = candidate;
    }
    ~AttributeList() { if (ptr) DeleteProcThreadAttributeList(ptr); }
};
bool safe_native_path(const fs::path& p, bool directory) {
    const auto attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
           (!!(attrs & FILE_ATTRIBUTE_DIRECTORY) == directory);
}
std::string read_capture(const fs::path& path) {
    if (!safe_native_path(path, false) || fs::file_size(path) > capture_limit)
        throw std::runtime_error("unsafe or oversized extractor output");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read extractor output");
    std::string data((std::istreambuf_iterator<char>(in)), {});
    if (in.bad() || data.size() > capture_limit)
        throw std::runtime_error("cannot read bounded extractor output");
    return data;
}
#endif
} // namespace

ExtractorResult run_verified_extractor(
    const fs::path& executable, std::string_view expected_sha256,
    const Args& args, const fs::path& capture_dir,
    std::chrono::seconds timeout) {
    if (expected_sha256.size() != 64 || timeout.count() <= 0 ||
        timeout > std::chrono::minutes(30))
        throw std::runtime_error("invalid extractor pin or timeout");
    if (!executable.is_absolute() || !capture_dir.is_absolute() ||
        !fs::is_regular_file(executable) || fs::is_symlink(executable) ||
        !fs::is_directory(capture_dir) || fs::is_symlink(capture_dir) ||
        sha256_file(executable) != expected_sha256)
        throw std::runtime_error("unverified extractor or unsafe capture directory");
#ifdef _WIN32
    if (!safe_native_path(executable, false) ||
        !safe_native_path(executable.parent_path(), true) ||
        !safe_native_path(capture_dir, true))
        throw std::runtime_error("extractor path contains a reparse point");
    const auto out_path = capture_dir / "extractor.stdout";
    const auto err_path = capture_dir / "extractor.stderr";
    // Capture directory is per invocation and must start empty. Existing
    // files are never truncated, overwritten or silently removed.
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    {
        Handle out(CreateFileW(out_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle err(CreateFileW(err_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle in(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!out.valid() || !err.valid() || !in.valid())
            throw std::runtime_error("cannot create extractor capture handles");
        HANDLE allowed[]{in.value, out.value, err.value};
        AttributeList attributes;
        if (!UpdateProcThreadAttribute(attributes.ptr, 0,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST, allowed, sizeof(allowed),
                nullptr, nullptr))
            throw std::runtime_error("cannot restrict extractor inherited handles");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = in.value;
        startup.StartupInfo.hStdOutput = out.value;
        startup.StartupInfo.hStdError = err.value;
        startup.lpAttributeList = attributes.ptr;
        PROCESS_INFORMATION pi{};
        auto cmd = command_line(executable, args);
        const auto cwd = executable.parent_path().wstring();
        if (!CreateProcessW(executable.c_str(), cmd.data(), nullptr, nullptr,
                            TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                            nullptr, cwd.c_str(), &startup.StartupInfo, &pi))
            throw std::runtime_error("extractor CreateProcessW failed: " +
                                     std::to_string(GetLastError()));
        Handle process(pi.hProcess), thread(pi.hThread);
        const auto max_ms = static_cast<DWORD>(
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        const auto start = GetTickCount64();
        DWORD code = 0;
        for (;;) {
            const auto wait = WaitForSingleObject(process.value, 200);
            LARGE_INTEGER nout{}, nerr{};
            const bool bounded = GetFileSizeEx(out.value, &nout) &&
                                 GetFileSizeEx(err.value, &nerr) &&
                                 nout.QuadPart <= static_cast<LONGLONG>(capture_limit) &&
                                 nerr.QuadPart <= static_cast<LONGLONG>(capture_limit);
            if (!bounded || (wait != WAIT_OBJECT_0 &&
                             (wait != WAIT_TIMEOUT || GetTickCount64()-start >= max_ms))) {
                TerminateProcess(process.value, 1);
                WaitForSingleObject(process.value, 5000);
                throw std::runtime_error("extractor timed out or exceeded output limit");
            }
            if (wait == WAIT_OBJECT_0) {
                if (!GetExitCodeProcess(process.value, &code))
                    throw std::runtime_error("cannot obtain extractor exit code");
                CloseHandle(out.value); out.value = INVALID_HANDLE_VALUE;
                CloseHandle(err.value); err.value = INVALID_HANDLE_VALUE;
                return {code, read_capture(out_path), read_capture(err_path)};
            }
        }
    }
#else
    (void)args;
    throw std::runtime_error("extractor execution is supported by the Windows launcher only");
#endif
}
} // namespace r2fix::gog
