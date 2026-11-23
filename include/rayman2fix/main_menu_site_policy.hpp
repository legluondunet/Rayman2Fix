#pragma once
// Exact five-locales text-handle policy from the owner's decoded
// Fix.sna + Fix.rtb: handle 362 is the original main-menu website.
// Never scan or replace arbitrary strings in video/Options scripts.
#include <cstdint>
#include <cstring>
namespace r2fix {
inline bool approved_main_menu_site_text(const char* s) noexcept {
    if(!s)return false;
    constexpr const char* original[5]{
        "/C:Site Rayman2",
        "/C:Web de Rayman2",
        "/C:Sito di Rayman2",
        "/C:Rayman2 - Webseite",
        "/C:Site Rayman2"};
    for(const char* expected:original)
        if(std::strcmp(s,expected)==0)return true;
    return false;
}
inline bool approved_replaced_main_menu_site_text(const char* s) noexcept {
    return s && std::strcmp(s,"/C:Rayman2Fix")==0;
}
// In the original shared Fix.sna (all five locales) the website caption
// is longer than its replacement. Modifying the verified *live text
// buffer* as well as the pointer ensures scene-local cached copies can
// be built from the new caption, including when returning from gameplay.
constexpr const char* kSiteCaptionReplacement="/C:Rayman2Fix";
inline bool site_redirect_approved_url(const char* url) noexcept {
    // Redirect only the old Rayman2 site action, not other ShellExecute
    // operations, local files, mailto links, browser windows or games.
    if(!url)return false;
    const char* p=url;
    auto ci=[&](const char* prefix)->bool {
        const char* a=p;const char* b=prefix;
        for(;*b;++a,++b) {
            if(!*a)return false;
            char x=*a,y=*b;
            if(x>='A'&&x<='Z')x=char(x-'A'+'a');
            if(x!=y)return false;
        }
        p=a;return true;
    };
    if(!(ci("http://")||ci("https://")||ci("www.")))return false;
    // If the URL starts www., p now points at its base domain.
    // Also accept subdomains in the scheme-qualified form.
    char host[96]{};unsigned n=0;
    while(*p && *p!='/' && *p!='?' && *p!='#' &&
          *p!=':' && n<sizeof(host)-1u) {
        char ch=*p++;
        if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');
        host[n++]=ch;
    }
    if(!n || n>=sizeof(host)-1u)return false;
    constexpr const char* approved[4]{
        "rayman2.com","www.rayman2.com",
        "rayman.com","www.rayman.com"};
    for(const char* h:approved)
        if(std::strcmp(host,h)==0)return true;
    return false;
}
} // namespace r2fix
