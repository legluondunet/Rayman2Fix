#pragma once
#include <cstdint>
namespace r2fix {
using SiteLogger=void (*)(const char*);
bool prepare_main_menu_site_caption(std::uintptr_t language_table,
                                    SiteLogger logger) noexcept;
bool install_main_menu_site(std::uintptr_t language_table,
                            SiteLogger logger) noexcept;
void restore_main_menu_site() noexcept;
 // Shared 1.5-second debounce for the native old-site callsite and the
 // explicit MENU / Pause selection actions. Prevent duplicate tabs.
bool claim_project_site_dispatch() noexcept;
}
