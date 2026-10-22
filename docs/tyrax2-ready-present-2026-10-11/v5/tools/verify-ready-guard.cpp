#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "tyrax2-ready-guard.hpp"
int main(){
  unsigned tests=0;
  for(unsigned mask=0;mask<8;++mask)for(unsigned sequence:{0u,1u,42u,0xffffffffu}){
    bool recording=mask&1,protect=mask&2,hud=mask&4;unsigned pending=sequence;
    const bool r=recording,p=protect,h=hud;
    {
      Tyra::ReadyRecordingSuspend guard(recording,protect,hud,pending);
      assert(!recording && protect && !hud && !pending);
      // An ordinary wait's flush and external synchronizer see no new prefix.
      bool wouldFlush=pending!=0;bool wouldRecurse=!recording && !protect;
      assert(!wouldFlush && !wouldRecurse);
      ++tests;
    }
    assert(recording==r && protect==p && hud==h && pending==sequence);
  }
  std::printf("READYGUARD %u state/reservation controls pass\n",tests);
}
