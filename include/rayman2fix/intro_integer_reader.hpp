#pragma once
#include <cstdint>

namespace r2fix {

// Read only integer designer variables using the exact Rayman 2 variable
// kind. The engine stores kinds 0/1 as signed bytes, 2 as unsigned byte,
// 3 as signed 16-bit, 4 as unsigned 16-bit, and 5/6 as signed 32-bit.
// Reject noninteger/pointer types rather than treating their low byte as
// a meaningful flag. Callbacks must validate the addresses.
template<class Read8,class Read32>
bool read_intro_integer(std::uintptr_t address,unsigned kind,
                        Read8 read8,Read32 read32,
                        std::int32_t& value) {
    if (address<0x10000 || kind>6) return false;
    std::uint8_t lo=0,hi=0;
    std::uint32_t word=0;
    if (kind==5 || kind==6) {
        if (!read32(address,word)) return false;
        value=static_cast<std::int32_t>(word);
        return true;
    }
    if (!read8(address,lo)) return false;
    if (kind==0 || kind==1) {
        value=static_cast<std::int8_t>(lo);
        return true;
    }
    if (kind==2) {
        value=lo;
        return true;
    }
    if (!read8(address+1,hi)) return false;
    const std::uint16_t combined=
        static_cast<std::uint16_t>(lo) |
        static_cast<std::uint16_t>(static_cast<unsigned>(hi)<<8);
    value=kind==3 ? static_cast<std::int16_t>(combined) : combined;
    return true;
}
} // namespace r2fix
