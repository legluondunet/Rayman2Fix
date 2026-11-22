#include <cstdlib>
#include "rayman2fix/graphics_menu_start_policy.hpp"

static void check(bool ok) { if(!ok) std::abort(); }

int main() {
    using r2fix::menu_scan_resume_cursor;
    // A prior 160-MiB pass must never restart from the low address
    // until the process address space has been traversed.
    check(menu_scan_resume_cursor(0x10000u,0x7FFF0000u)==0x10000u);
    check(menu_scan_resume_cursor(0x0A234000u,0x7FFF0000u)==0x0A234000u);
    check(menu_scan_resume_cursor(0x17FF0000u,0x7FFF0000u)==0x17FF0000u);
    check(menu_scan_resume_cursor(0x7FFF0000u,0x7FFF0000u)==0x10000u);
    check(menu_scan_resume_cursor(0x0u,0x7FFF0000u)==0x10000u);
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
    using r2fix::menu_controller_worker_continue;
    using r2fix::menu_controller_scan_allowed;
    // The trace-only observer stops at the configured time/report cap.
    check(menu_controller_worker_continue(false,899,20,900,160));
    check(!menu_controller_worker_continue(false,900,20,900,160));
    check(!menu_controller_worker_continue(false,50,160,900,160));
    // A user can return to MENU long after three minutes or 160 logs.
    check(menu_controller_worker_continue(true,900,160,900,160));
    check(menu_controller_worker_continue(true,100000,100000,900,160));
    // In the persistent variant, memory scans must occur ONLY in MENU.
    check(menu_controller_scan_allowed(true,true,0,18));
    check(!menu_controller_scan_allowed(true,false,0,18));
    check(!menu_controller_scan_allowed(true,true,18,18));
    // The legacy bounded observer still accepts its previous scope.
    check(menu_controller_scan_allowed(false,false,0,18));
    check(!menu_controller_scan_allowed(false,true,18,18));
    return 0;
}
