#include "rayman2fix/standalone_video_device_policy.hpp"
#include <cstdlib>
#include <cstring>
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
    check(display_pci_revision("PCI\\VEN_1002&DEV_1900&REV_C5")==0xC5);
    check(display_pci_revision("pci\\ven_1002&dev_1900&rev_ba\\2&0")==0xBA);
    check(display_pci_revision("PCI\\VEN_1002&DEV_1900")==-1);
    check(display_pci_revision("REV_C")==-1);
    check(display_pci_revision("REV_C55")==-1);
    check(display_pci_revision("REV_Z5")==-1);
    check(display_pci_revision(nullptr)==-1);
    const auto m780=amd_hawkpoint_model("PCI\\VEN_1002&DEV_1900&REV_C5");
    const auto m760=amd_hawkpoint_model("PCI\\VEN_1002&DEV_1900&REV_C1");
    const auto mba=amd_hawkpoint_model("PCI\\VEN_1002&DEV_1900&REV_BA");
    check(m780 && std::strcmp(m780,"AMD Radeon 780M")==0);
    check(m760 && std::strcmp(m760,"AMD Radeon 760M")==0);
    check(mba && std::strcmp(mba,"AMD Radeon 780M")==0);
    check(!amd_hawkpoint_model("PCI\\VEN_1002&DEV_1900"));
    check(!amd_hawkpoint_model("PCI\\VEN_1002&DEV_1900&REV_FF"));
    check(!amd_hawkpoint_model("PCI\\VEN_10DE&DEV_1900&REV_C5"));
    check(!amd_hawkpoint_model("PCI\\VEN_1002&DEV_1901&REV_C5"));
    return 0;
}
