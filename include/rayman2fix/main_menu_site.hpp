#pragma once
#include <cstdint>
namespace r2fix {
using SiteLogger=void (*)(const char*);
bool install_main_menu_site(std::uintptr_t language_table,
                            SiteLogger logger) noexcept;
void restore_main_menu_site() noexcept;
}
