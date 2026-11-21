#include <cstdlib>
#include "rayman2fix/graphics_menu_start_policy.hpp"

static void check(bool ok) { if(!ok) std::abort(); }

int main() {
    using r2fix::menu_controller_worker_required;
    using r2fix::menu_passive_diagnostics_required;
    // Release default: Log=0; the VIDEO information feature and its
    // exact-script mode protection MUST still start.
    check(menu_controller_worker_required(false,true));
    check(!menu_passive_diagnostics_required(false));
    // Debug mode keeps the optional passive observers as before.
    check(menu_controller_worker_required(true,true));
    check(menu_passive_diagnostics_required(true));
    // A trace-only build with logging off should not create workers.
    check(!menu_controller_worker_required(false,false));
    check(!menu_passive_diagnostics_required(false));
    // Explicit logging on a trace-only build enables both observers.
    check(menu_controller_worker_required(true,false));
    check(menu_passive_diagnostics_required(true));
    return 0;
}
