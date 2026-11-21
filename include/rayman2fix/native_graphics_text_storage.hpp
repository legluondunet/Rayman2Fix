#pragma once
// Pure, testable backing-storage operation for the native Menu localization
// patch. The game's pointers to live[i].data() remain unchanged across
// video / brightness / other-menu transitions. No game code is required.
#include <array>
#include <cstddef>
namespace r2fix {
template<std::size_t Rows,std::size_t Capacity>
void switch_stable_menu_labels(
    std::array<std::array<char,Capacity>,Rows>& live,
    const std::array<std::array<char,Capacity>,Rows>& saved) noexcept {
    for(std::size_t i=0;i<Rows;++i)
        live[i]=saved[i];
}
} // namespace r2fix
