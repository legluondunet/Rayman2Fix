#include "rayman2fix/main_menu_site_policy.hpp"
#include <cstdlib>
#include <initializer_list>
using namespace r2fix;
void check(bool b){if(!b)std::abort();}
int main(){
    for(const char* name:{
        "/C:Site Rayman2","/C:Web de Rayman2",
        "/C:Sito di Rayman2","/C:Rayman2 - Webseite"}) {
        check(approved_main_menu_site_text(name));
    }
    check(!approved_main_menu_site_text("/C:VIDEO"));
    check(!approved_main_menu_site_text("/C:Site Rayman3"));
    check(!approved_main_menu_site_text(nullptr));
    for(const char* uri:{
        "https://www.rayman2.com/","http://rayman.com/",
        "www.rayman.com"})check(site_redirect_approved_url(uri));
    for(const char* uri:{
        "https://github.com/legluondunet/Rayman2Fix",
        "https://other-rayman.com","mailto:rayman@rayman.com",
        "rayman2.exe","https://rayman2.com.evil.example/"}) {
        check(!site_redirect_approved_url(uri));
    }
    return 0;
}
