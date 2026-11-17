#include "rayman2fix/intro_integer_reader.hpp"
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <unordered_map>

// Explicitly check in both Debug and Release (assert is disabled by NDEBUG).
#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "CHECK failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        std::abort(); \
    } \
} while (false)

struct Mock {
    std::unordered_map<std::uintptr_t,std::uint8_t> bytes;
    std::unordered_map<std::uintptr_t,std::uint32_t> words;
    bool read8(std::uintptr_t p,std::uint8_t& v) const {
        const auto it=bytes.find(p);
        if(it==bytes.end())return false;
        v=it->second;return true;
    }
    bool read32(std::uintptr_t p,std::uint32_t& v) const {
        const auto it=words.find(p);
        if(it==words.end())return false;
        v=it->second;return true;
    }
};
int main() {
    constexpr std::uintptr_t p=0x20000;
    Mock m{};
    auto read=[&](unsigned kind,std::int32_t& v){
        return r2fix::read_intro_integer(p,kind,
            [&](std::uintptr_t x,std::uint8_t& y){return m.read8(x,y);},
            [&](std::uintptr_t x,std::uint32_t& y){return m.read32(x,y);},
            v);
    };
    std::int32_t v=1234;
    m.bytes[p]=0xFF;
    CHECK(read(0,v) && v==-1);
    CHECK(read(1,v) && v==-1);
    CHECK(read(2,v) && v==255);
    m.bytes[p+1]=0xFF;
    CHECK(read(3,v) && v==-1);
    CHECK(read(4,v) && v==65535);
    m.words[p]=0xFFFFFFFCu;
    CHECK(read(5,v) && v==-4);
    CHECK(read(6,v) && v==-4);
    CHECK(!read(7,v) && !read(8,v) && !read(9,v));
    std::int32_t untouched=55;
    CHECK(!r2fix::read_intro_integer(0,0,
        [&](std::uintptr_t x,std::uint8_t& y){return m.read8(x,y);},
        [&](std::uintptr_t x,std::uint32_t& y){return m.read32(x,y);},
        untouched));
    CHECK(untouched==55);
    m.bytes.erase(p+1);
    CHECK(!read(3,v));
    m.words.clear();
    CHECK(!read(5,v));
}
