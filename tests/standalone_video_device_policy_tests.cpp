#include "rayman2fix/standalone_video_device_policy.hpp"
#include <cstdlib>
using namespace r2fix;
void check(bool ok) {if(!ok)std::abort();}
int main() {
    check(matching_display_pci_identity(
        "PCI\\VEN_1002&DEV_73FF&SUBSYS_12041002",
        "pci\\ven_1002&dev_73ff&subsys_12041002\\2&0"));
    check(!matching_display_pci_identity(
        "PCI\\VEN_1002&DEV_73FF","PCI\\VEN_1002&DEV_164C"));
    check(!matching_display_pci_identity(
        "PCI\\VEN_1002&DEV_73FF","PCI\\VEN_10DE&DEV_73FF"));
    check(!matching_display_pci_identity(
        "DISPLAY\\DEFAULT_MONITOR","PCI\\VEN_1002&DEV_73FF"));
    check(!matching_display_pci_identity(nullptr,"PCI\\VEN_1002&DEV_73FF"));
    return 0;
}
