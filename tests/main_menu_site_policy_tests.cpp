#include "rayman2fix/main_menu_site_policy.hpp"
#include "rayman2fix/main_menu_site_script_policy.hpp"
#include <vector>
#include <cstdlib>
#include <initializer_list>
using namespace r2fix;
void check(bool b){if(!b)std::abort();}
int main(){
    for(const char* name:{
        "/C:Site Rayman2","/C:Web de Rayman2",
        "/C:Sito di Rayman2","/C:Rayman2 - Webseite",
        "/C:Internet","/C:INTERNET","/C:internet"}) {
        check(approved_main_menu_site_text(name));
    }
    check(approved_replaced_main_menu_site_text("/C:Rayman2Fix"));
    check(!approved_replaced_main_menu_site_text("/C:Site Rayman2"));
    check(!approved_replaced_main_menu_site_text(nullptr));
    check(std::strcmp(kSiteCaptionReplacement,"/C:Rayman2Fix")==0);
    check(std::strlen("/C:Internet")<std::strlen(kSiteCaptionReplacement));
    check(!approved_main_menu_site_text("/C:Internet settings"));
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
    std::vector<NativeMenuScriptNode> original(kSiteScriptNodes);
    original[kSiteProcedureNode]={369u,0u,7u,4u};
    original[kSiteUrlNode]={0x221122u,0u,8u,20u};
    original.back()={0u,0u,0u,8u};
    const auto hash=menu_site_script_shape(original.data(),
                                           kSiteScriptNodes);
    check(redirect_menu_site_script(original.data(),kSiteScriptNodes,
          0x221122u,0x334433u,hash));
    check(original[kSiteUrlNode].parameter==0x334433u);
    check(!redirect_menu_site_script(original.data(),kSiteScriptNodes,
          0x221122u,0x334433u,hash));
    original[kSiteUrlNode].parameter=0x221122u;
    original[kSiteProcedureNode].parameter=368u;
    check(!redirect_menu_site_script(original.data(),kSiteScriptNodes,
          0x221122u,0x334433u,hash));
    original[kSiteProcedureNode].parameter=369u;
    original[kSiteUrlNode].type=12u;
    check(!redirect_menu_site_script(original.data(),kSiteScriptNodes,
          0x221122u,0x334433u,hash));
    original[kSiteUrlNode].type=20u;
    check(!redirect_menu_site_script(original.data(),100u,
          0x221122u,0x334433u,hash));
    return 0;
}
