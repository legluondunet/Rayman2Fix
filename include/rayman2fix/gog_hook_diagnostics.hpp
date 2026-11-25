#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace r2fix::gog {
// Diagnostic only. CD signatures copied from the existing runtime.cpp
// and video_navigation_input.cpp guards. A match is NOT authorization
// to install hooks or a proof of full ABI compatibility.
struct HookSignature {
    const char* name;
    std::uint32_t rva;
    std::vector<unsigned char> bytes;
};
inline const std::vector<HookSignature>& diagnostic_signatures() {
    static const std::vector<HookSignature> sites{
        {"camera",0x22AB0,{0x83,0xEC,0x0C,0x8B,0x4C,0x24,0x14}},
        {"native-2d",0x29670,{0x83,0xEC,0x50,0x8B,0x44,0x24,0x54}},
        {"screen-to-vertex",0x22940,{0x81,0xEC,0xD4,0,0,0}},
        {"superimposed",0x710C0,{0x83,0xEC,0x28,0x8D,0x44,0x24,0x30}},
        {"frame-flip",0x20F50,{0x8B,0x0D,0x08,0x72,0x4A,0}},
        {"cinematic-viewport",0x20660,{0x83,0xEC,0x08,0x53,0x55,0x56}},
        {"ipt-predicate",0x2E7AD,{0xE8,0x0E,0x03,0,0}},
        {"ipt-x",0x6E755,{0xE8,0x76,0x01,0xFC,0xFF}},
        {"ipt-y",0x6E774,{0xE8,0x57,0x01,0xFC,0xFF}},
        {"ipt-poll",0x96ACA,{0xE8,0x11,0x03,0,0}},
        {"video-navigation",0x966B3,{0x3D,0x0C,0,0x07,0x80,0x7F,0x0B}}
    };
    return sites;
}

class PeDiagnostic {
public:
    explicit PeDiagnostic(std::vector<unsigned char> bytes) : bytes_(std::move(bytes)) {
        if (u16(0) != 0x5a4d) fail();
        pe_ = u32(0x3c);
        if (pe_ < 64 || u32(pe_) != 0x4550 || u16(pe_+4) != 0x14c) fail();
        sections_ = u16(pe_+6);
        const auto opt_size = u16(pe_+20);
        opt_ = pe_+24;
        if (!sections_ || sections_ > 96 || opt_size < 96 ||
            u16(opt_) != 0x10b) fail();
        table_ = opt_+opt_size;
        need(table_, std::size_t(sections_)*40);
    }
    std::uint32_t timestamp() const { return u32(pe_+8); }
    std::uint32_t image_size() const { return u32(opt_+56); }
    std::uint32_t image_base() const { return u32(opt_+28); }
    std::uint32_t entry_rva() const { return u32(opt_+16); }
    std::string check(const HookSignature& site) const {
        for (unsigned i=0;i<sections_;++i) {
            const auto s=table_+40u*i;
            const auto va=u32(s+12), raw_size=u32(s+16), raw=u32(s+20);
            if (site.rva<va) continue;
            const auto delta=std::uint64_t(site.rva)-va;
            if (delta>raw_size || site.bytes.size()>raw_size-delta) continue;
            if (!(u32(s+36)&0x20000000u)) return "non-executable";
            const auto off=std::uint64_t(raw)+delta;
            if (off>bytes_.size() || site.bytes.size()>bytes_.size()-off)
                return "out-of-file";
            for (std::size_t j=0;j<site.bytes.size();++j)
                if (bytes_[std::size_t(off)+j]!=site.bytes[j]) return "mismatch";
            return "match";
        }
        return "not-file-backed";
    }
private:
    std::vector<unsigned char> bytes_;
    std::size_t pe_=0,opt_=0,table_=0;
    unsigned sections_=0;
    [[noreturn]] static void fail() { throw std::runtime_error("invalid PE32 diagnostic image"); }
    void need(std::size_t off,std::size_t count) const {
        if (off>bytes_.size() || count>bytes_.size()-off) fail();
    }
    std::uint16_t u16(std::size_t off) const {
        need(off,2); return std::uint16_t(bytes_[off]) | (std::uint16_t(bytes_[off+1])<<8);
    }
    std::uint32_t u32(std::size_t off) const {
        need(off,4); return std::uint32_t(u16(off)) | (std::uint32_t(u16(off+2))<<16);
    }
};

inline std::vector<std::string> hook_diagnostics(const std::filesystem::path& exe) {
    if (std::filesystem::file_size(exe)>8u*1024u*1024u)
        throw std::runtime_error("oversized PE diagnostic");
    std::ifstream in(exe,std::ios::binary);
    if (!in) throw std::runtime_error("cannot read PE for diagnostics");
    std::vector<unsigned char> bytes(
        static_cast<std::size_t>(std::filesystem::file_size(exe)));
    if (!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("truncated PE diagnostic");
    const PeDiagnostic pe(std::move(bytes));
    std::ostringstream header;
    header<<"GOG-PE: timestamp=0x"<<std::hex<<pe.timestamp()
          <<" image_size=0x"<<pe.image_size()<<" image_base=0x"<<pe.image_base()
          <<" entry_rva=0x"<<pe.entry_rva()
          <<" cd_timestamp_match="<<(pe.timestamp()==0x3807B68Bu)
          <<" cd_image_size_match="<<(pe.image_size()==0x1D7000u);
    std::vector<std::string> lines{header.str()};
    unsigned matched=0;
    for (const auto& site:diagnostic_signatures()) {
        const auto status=pe.check(site);
        matched += status=="match";
        std::ostringstream line;
        line<<"GOG-HOOK-PROBE: "<<site.name<<" rva=0x"<<std::hex<<site.rva
            <<" status="<<status;
        lines.push_back(line.str());
    }
    lines.push_back("GOG-HOOK-PROBE: matched="+std::to_string(matched)+"/"+
                    std::to_string(diagnostic_signatures().size())+
                    " diagnostic-only; full runtime ABI not authorized");
    return lines;
}
} // namespace r2fix::gog
