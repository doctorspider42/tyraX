// Host-only actual RendererCore successful-warp prefix; no target timing claim.
#include <cassert>
#include <cstdint>
#include <cstdio>
using u32=uint32_t;
struct WarpCamera{};
struct Threading{static void switchThread(){}};
struct RendererCore{
 u32 recordingGeneration=0;bool hasPresentedFrame=false,frameYield=false;
 struct Settings{bool hybrid=false;bool isHybridOutput()const{return hybrid;}}settings;
 struct Warp{unsigned draws=0;void draw(const WarpCamera&,const WarpCamera&){++draws;}}warp;
 unsigned completions=0;void completePipelineFrame(){++completions;}
 bool presentWarpFrame(const WarpCamera&,const WarpCamera&);
};
#include "actual-warp.inc"
int main(){RendererCore c;WarpCamera a,b;
 assert(!c.presentWarpFrame(a,b)&&c.recordingGeneration==0&&c.warp.draws==0);
 c.hasPresentedFrame=true;c.settings.hybrid=true;
 assert(!c.presentWarpFrame(a,b)&&c.recordingGeneration==0&&c.warp.draws==0);
 c.settings.hybrid=false;assert(c.presentWarpFrame(a,b)&&c.recordingGeneration==1&&c.warp.draws==1);
 c.recordingGeneration=UINT32_MAX;c.frameYield=true;
 assert(c.presentWarpFrame(a,b)&&c.recordingGeneration==0&&c.warp.draws==2&&c.completions==4);
 std::puts("PASS actual successful-warp prefix; failed branches unchanged; generation wrap");
}
