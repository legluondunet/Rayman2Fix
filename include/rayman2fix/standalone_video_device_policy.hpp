#pragma once
// Match the active Win32 display's PCI vendor/device before attributing
// any installed Windows display driver version to it.
namespace r2fix {
constexpr char pci_upper(char c) noexcept {
    return c>='a' && c<='z'?char(c-'a'+'A'):c;
}
constexpr bool pci_hex(char c) noexcept {
    c=pci_upper(c);
    return (c>='0'&&c<='9')||(c>='A'&&c<='F');
}
inline const char* pci_token(const char* s,const char* token) noexcept {
    if(!s||!token)return nullptr;
    for(const char* p=s;*p;++p) {
        bool found=true;
        for(unsigned i=0;i<4u;++i)
            if(!p[i]||pci_upper(p[i])!=token[i]) {found=false;break;}
        if(!found)continue;
        for(unsigned i=0;i<4u;++i)
            if(!p[4u+i]||!pci_hex(p[4u+i])) {found=false;break;}
        if(found)return p+4u;
    }
    return nullptr;
}
inline bool matching_display_pci_identity(
    const char* display_id,const char* device_id) noexcept {
    const char* av=pci_token(display_id,"VEN_");
    const char* bv=pci_token(device_id,"VEN_");
    const char* ad=pci_token(display_id,"DEV_");
    const char* bd=pci_token(device_id,"DEV_");
    if(!av||!bv||!ad||!bd)return false;
    for(unsigned i=0;i<4u;++i)
        if(pci_upper(av[i])!=pci_upper(bv[i]) ||
           pci_upper(ad[i])!=pci_upper(bd[i]))return false;
    return true;
}
} // namespace r2fix
