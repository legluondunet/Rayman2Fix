#include "rayman2fix/post_pause_ui_bar_probe_policy.hpp"
#include <cstdlib>
#include <limits>
using namespace r2fix;
static void ck(bool v){if(!v)std::abort();}
int main(){
    ck(post_pause_ui_bar_probe(true,false,1u,1u,
                               60.0f,85.0f,75.0f,90.0f));
    ck(post_pause_ui_bar_probe(true,false,1u,1u,
                               75.0f,85.0f,90.0f,90.0f));
    ck(post_pause_ui_bar_probe(true,false,2u,0u,
                               55.0f,73.0f,83.0f,78.0f));
    ck(!post_pause_ui_bar_probe(false,false,1u,1u,
                               60.0f,85.0f,75.0f,90.0f));
    ck(!post_pause_ui_bar_probe(true,true,1u,1u,
                               60.0f,85.0f,75.0f,90.0f));
    ck(!post_pause_ui_bar_probe(true,false,0u,1u,
                               60.0f,85.0f,75.0f,90.0f));
    ck(!post_pause_ui_bar_probe(true,false,2u,0u,
                               0.0f,0.0f,100.0f,100.0f));
    ck(!post_pause_ui_bar_probe(true,false,1u,2u,
                               10.0f,8.0f,20.0f,13.0f));
    ck(!post_pause_ui_bar_probe(true,false,1u,8u,
                               60.0f,85.0f,75.0f,90.0f));
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    ck(!post_pause_ui_bar_probe(true,false,1u,1u,
                               60.0f,nan,75.0f,90.0f));
    return 0;
}
