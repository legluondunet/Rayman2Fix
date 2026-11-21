#include "rayman2fix/graphics_menu_target.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}

int main() {
    using namespace r2fix;
    // Only synthetic metadata; no commercial binaries in the repository.
    constexpr GraphicsMenuPeFingerprint protected_cd{
        kPe32I386,3,kGameTimestamp,kPe32OptionalMagic,kGameEntryRva,
        kOriginalCdGameImageSize};
    constexpr GraphicsMenuPeFingerprint deprotected{
        kPe32I386,6,kGameTimestamp,kPe32OptionalMagic,kGameEntryRva,
        kDeprotectedGameImageSize};
    static_assert(!graphics_menu_accepts_deprotected_image(protected_cd),
                  "Original CD PE is not the runtime target");
    static_assert(graphics_menu_accepts_deprotected_image(deprotected),
                  "Approved deprotected PE must be accepted");
    check(!graphics_menu_accepts_deprotected_image({
        kPe32I386,3,kGameTimestamp,kPe32OptionalMagic,kGameEntryRva,
        kDeprotectedGameImageSize}),"wrong section layout rejected");
    check(!graphics_menu_accepts_deprotected_image({
        kPe32I386,6,0,kPe32OptionalMagic,kGameEntryRva,
        kDeprotectedGameImageSize}),"wrong timestamp rejected");
    check(!graphics_menu_accepts_deprotected_image({
        0x8664,6,kGameTimestamp,kPe32OptionalMagic,kGameEntryRva,
        kDeprotectedGameImageSize}),"x64 rejected");
    check(!graphics_menu_accepts_deprotected_image({
        kPe32I386,6,kGameTimestamp,kPe32OptionalMagic,0,
        kDeprotectedGameImageSize}),"incorrect entry point rejected");
    std::cout << "Rayman 2 deprotected PE menu tracer fingerprint: OK\n";
    return 0;
}
