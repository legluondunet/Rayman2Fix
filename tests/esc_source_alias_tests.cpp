#include "rayman2fix/esc_source_alias.hpp"
#include <cstdint>
#include <cstdlib>
using namespace r2fix::esc_source_alias;
static void require(bool b) { if(!b)std::abort(); }
static void set32(RawTable& t,unsigned index,unsigned offset,std::uint32_t v) {
    const unsigned p=index*unsigned(kStride)+offset;
    for(unsigned byte=0;byte<4;++byte)t[p+byte]=std::uint8_t(v>>(8u*byte));
}
static void eligible(RawTable& t,unsigned index,std::uint32_t value,
                     unsigned stamp=1u) {
    set32(t,index,0,value);
    set32(t,index,8,0);
    set32(t,index,0x10,1);
    set32(t,index,0x14,0x1000u);
    set32(t,index,0x1c,stamp);
}
int main() {
    RawTable t{};
    const std::uint32_t ref=0x123456u;
    eligible(t,2,ref);
    eligible(t,50,ref);
    eligible(t,51,0x234567u);
    auto o=summarize(t,true,ref,1);
    require(o.sampled && o.same_ref==2 &&
            o.transition_candidate==2 && o.stamped_candidate==2);
    require(o.indices[0]==2 && o.indices[1]==50 && o.indices[2]==-1);
    require(!o.authorize_audio);
    // Same pointer but source excluded, still a post-wrapper alias.
    set32(t,50,8,1);
    o=summarize(t,true,ref,1);
    require(o.same_ref==2 && o.transition_candidate==1 &&
            o.stamped_candidate==1 && !o.authorize_audio);
    // Stale (or unrelated) source stamp is not a fresh candidate.
    set32(t,2,0x1c,5);
    o=summarize(t,true,ref,1);
    require(o.same_ref==2 && o.transition_candidate==1 &&
            o.stamped_candidate==0);
    // Torn read, invalid reference, or unknown old depth fails closed.
    require(!summarize(t,false,ref,1).sampled);
    require(!summarize(t,true,0,1).sampled);
    require(!summarize(t,true,ref,0).sampled);
    for(unsigned i=0;i<64;++i)eligible(t,i,ref);
    o=summarize(t,true,ref,1);
    require(o.sampled && o.same_ref==64 &&
            o.stamped_candidate==64 && o.indices[3]==3);
    require(!o.authorize_audio);

    // The same original 64-record Pause walk can reach the *same*
    // physical SXD transition twice: one direct source + one Theme-11
    // generic dispatcher. Only the direct +0 ref aliases the WAVMW
    // callback object; the Theme's +0 refers to its own controller.
    RawTable paired{};
    eligible(paired,1,ref,1);  // direct transition record
    // A separate active, generic source with a Theme controller ref.
    set32(paired,5,0,0x456780u);
    set32(paired,5,0x08,0);
    set32(paired,5,0x10,0);
    set32(paired,5,0x14,0x654321u);
    set32(paired,5,0x1c,1);
    std::array<int,kRows> classes{};
    classes.fill(-1);
    classes[5]=11;
    const auto direct=summarize(paired,true,ref,1);
    const auto theme=summarize_theme_routes(paired,classes,true,1);
    require(direct.sampled && direct.same_ref==1 &&
            direct.stamped_candidate==1 && direct.indices[0]==1);
    require(theme.sampled && theme.generic_candidate==1 &&
            theme.theme_type11==1 && theme.theme_stamped==1 &&
            theme.indices[0]==5 && !theme.authorize_audio);
    // A pending or unknown nested descriptor is no evidence.
    classes[5]=-1;
    const auto unknown=summarize_theme_routes(paired,classes,true,1);
    require(unknown.sampled && unknown.unknown_class==1 &&
            unknown.theme_type11==0 && !unknown.authorize_audio);
    // No stale readings, invalid depth, or changed source flags
    // may be interpreted as verified Theme ownership.
    require(!summarize_theme_routes(paired,classes,false,1).sampled);
    require(!summarize_theme_routes(paired,classes,true,0).sampled);
    classes[5]=11;
    set32(paired,5,8,1);
    require(summarize_theme_routes(paired,classes,true,1)
            .theme_type11==0);
    set32(paired,5,8,0);
    set32(paired,5,0x1c,9);
    const auto stamped=summarize_theme_routes(paired,classes,true,1);
    require(stamped.theme_type11==1 && stamped.theme_stamped==0 &&
            !stamped.authorize_audio);
}
