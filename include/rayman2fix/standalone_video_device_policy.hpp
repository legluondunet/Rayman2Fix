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
// PCI REV_ is two hex digits (unlike VEN_ and DEV_).
inline int display_pci_revision(const char* id) noexcept {
    if(!id)return -1;
    for(const char* p=id;*p;++p) {
        if(pci_upper(p[0])!='R' || !p[1] || pci_upper(p[1])!='E' ||
           !p[2] || pci_upper(p[2])!='V' || !p[3] || p[3]!='_')continue;
        if(!pci_hex(p[4]) || !p[4] || !pci_hex(p[5]))return -1;
        if(p[6] && p[6]!='&' && p[6]!='\\')return -1;
        const auto digit=[](char c){return c<='9'?c-'0':pci_upper(c)-'A'+10;};
        return digit(p[4])*16+digit(p[5]);
    }
    return -1;
}
// Marketing names from AMD's libdrm amdgpu.ids update:
// a7eb2cfd53a70fcd9ba9dcfad80a3994642f362f (2025-01-17),
// plus subsequent AMD BA/BB entries. Never infer a SKU from DEV alone.
inline const char* amd_hawkpoint_model(const char* id) noexcept {
    if(!matching_display_pci_identity(id,"PCI\\VEN_1002&DEV_1900"))return nullptr;
    switch(display_pci_revision(id)) {
        case 0x02:case 0x04:case 0x07:case 0xB7:case 0xB8:
        case 0xC1:case 0xC3:case 0xC6:case 0xC8:case 0xCA:case 0xCD:case 0xCF:
        case 0xD1:case 0xD3:case 0xD6:case 0xD8:case 0xDA:case 0xDD:case 0xDF:
            return "AMD Radeon 760M";
        case 0x01:case 0x03:case 0x05:case 0x06:
        case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:
        case 0xB9:case 0xBA:case 0xBB:case 0xC0:case 0xC2:case 0xC4:
        case 0xC5:case 0xC7:case 0xC9:case 0xCB:case 0xCC:case 0xCE:
        case 0xD0:case 0xD2:case 0xD4:case 0xD5:case 0xD7:case 0xD9:
        case 0xDB:case 0xDC:case 0xDE:case 0xF0:case 0xF1:case 0xF2:
            return "AMD Radeon 780M";
        default:return nullptr;
    }
}
} // namespace r2fix
