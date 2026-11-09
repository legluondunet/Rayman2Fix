#include "rayman2fix/extraction_console.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace r2fix {
namespace {
std::string mib(std::uint64_t bytes) {
    std::ostringstream value;
    value << std::fixed << std::setprecision(1)
          << (static_cast<double>(bytes) / 1048576.0) << " MiB";
    return value.str();
}
} // namespace

ExtractionConsole::ExtractionConsole(std::ostream& output)
    : out_(output), start_(std::chrono::steady_clock::now()) {}

void ExtractionConsole::step(unsigned number, unsigned total,
                             const std::string& description) {
    out_ << '[' << number << '/' << total << "] " << description << '\n';
    out_.flush();
}

void ExtractionConsole::plan(const DiscInfo& info) {
    out_ << "  Volume: " << info.volume_id
         << " | format: " << info.format
         << " | selected: " << info.files.size() - info.skipped_files
         << " files (" << mib(info.extract_bytes) << ')'
         << " | excluded: " << info.skipped_files << " files\n";
    out_.flush();
}

void ExtractionConsole::progress(const ExtractionProgress& current) {
    if (!began_) {
        began_ = true;
        out_ << "  Progress: 0% (0/" << current.total_files << " files)\n";
    }
    const std::uint64_t numerator = current.total_bytes ?
                                   std::min(current.copied_bytes, current.total_bytes) :
                                   current.copied_files;
    const std::uint64_t denominator = current.total_bytes ?
                                     current.total_bytes : current.total_files;
    unsigned percentage = denominator ?
        static_cast<unsigned>((numerator * 100) / denominator) : 100;
    // 100% belongs to the completed and published extraction, not to an
    // in-flight file before the staging directory has been renamed.
    percentage = std::min(percentage, 99u);
    if (percentage < next_percent_) {
        out_.flush();
        return;
    }
    next_percent_ = (percentage / 5 + 1) * 5;
    out_ << "  Progress: " << percentage << "% ("
         << current.copied_files << '/' << current.total_files
         << " files, " << mib(current.copied_bytes) << '/'
         << mib(current.total_bytes) << ')';
    if (!current.current_file.empty())
        out_ << " | " << current.current_file.generic_u8string();
    out_ << '\n';
    out_.flush();
}

void ExtractionConsole::summary(const DiscInfo& info, const ExtractionResult& result,
                                const std::filesystem::path& destination) {
    if (result.copied_files != info.files.size() - info.skipped_files ||
        result.copied_bytes != info.extract_bytes ||
        result.skipped_files != info.skipped_files ||
        result.skipped_bytes != info.skipped_bytes)
        throw std::runtime_error("extraction summary does not match completed output");

    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_).count();
    out_ << "\nExtraction summary\n"
         << "  Status: success (100%)\n"
         << "  Files extracted: " << result.copied_files << '\n'
         << "  Data copied: " << mib(result.copied_bytes)
         << " (" << result.copied_bytes << " bytes)\n"
         << "  Files excluded: " << result.skipped_files << '\n'
         << "  Data excluded: " << mib(result.skipped_bytes) << '\n'
         << "  Destination: " << destination.u8string() << '\n'
         << "  Elapsed: " << std::fixed << std::setprecision(1)
         << seconds << " s\n\n";
    out_.flush();
}
}  // namespace r2fix
