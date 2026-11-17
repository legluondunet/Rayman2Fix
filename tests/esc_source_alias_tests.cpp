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
}
