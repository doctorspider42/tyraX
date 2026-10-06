#include <stdint.h>
#include <vector>
#include <cstdio>
#define PRIVATE_PMU_HOST 1
namespace NightAblation {inline bool valid=true;}
namespace NightSampler {inline uint32_t frame=0;}
// UNRELEASED private PMU source draft. I-side selector6 and D-side selector6
// bus-read/cache-miss events (includes uncached loads) are raw activation data.
// 31-bit/unmaskable overflow hazard: enabled-lifetime qualification unresolved.

namespace NightPMU {
using U=uint32_t;
constexpr U stoppedPcr=0x000340d0U,activePcr=0x800340d0U;
struct Row {U frame,scene,i0,d0,i1,d1,reads;};
inline Row rows[128]{};
inline U phase=0,frameCalls=0,phaseCalls=0,phaseReads=0,rowCount=0;
inline U commonControlReads=0,resets=0,enables=0,stops=0;
inline U beforePcr=0,preSetupPcr=0,setupPcr=0,endPcr=0,setupCount=0;
inline U activeFrame=0,activeScene=0,cleanupPcr=0;
inline bool selected=false,enabled=false,inWindow=false,active=false,knownStopped=false,cleanupVerified=false;
// Permanent fail-closed latch; only process initialization resets it.
inline bool failed=false;
#ifdef PRIVATE_PMU_HOST
U hostReadPcr();U hostReadI();U hostReadD();void hostConfigureStopped();void hostResetEnable();void hostStopOwned();
inline U readPcr(){return hostReadPcr();}
inline U readI(){return hostReadI();}
inline U readD(){return hostReadD();}
inline void configureStopped(){hostConfigureStopped();}
inline void resetEnable(){hostResetEnable();}
inline void stopOwned(){hostStopOwned();}
#else
// Pinned Sony EE manual + ps2dev/binutils-gdb provide syntax/encoding.
// Actual linked placement and safe enabled lifetime are separate root gates.
inline U readPcr(){U v;asm volatile(".set push\n.set noreorder\nmfps %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readI(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readD(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,1\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline void configureStopped(){const U v=stoppedPcr;asm volatile(".set push\n.set noreorder\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
// Caller has just observed locally owned STOPPED state. Never reset enabled
// counters; no COP0 Count write, no interrupt masking and no consumer fence.
inline void resetEnable(){const U v=activePcr;asm volatile(".set push\n.set noreorder\nmtpc $0,0\nmtpc $0,1\nsync.p\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
inline void stopOwned(){const U v=stoppedPcr;asm volatile(".set push\n.set noreorder\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
#endif
inline void fail(){failed=true;NightAblation::valid=false;}
inline void begin(U index,bool on){
 const U p=index/1800,o=index%1800;inWindow=false;frameCalls=0;
 if(!selected||p>=3||failed)return;
 if(o==0){if(active)fail();phase=p;enabled=on;phaseCalls=phaseReads=rowCount=setupCount=commonControlReads=resets=enables=stops=0;beforePcr=preSetupPcr=setupPcr=endPcr=0;for(auto& r:rows)r=Row{};}
 if(o==750)beforePcr=readPcr();
 if(o==799){preSetupPcr=readPcr();
  if(active||(preSetupPcr&0x80000000U)){fail();knownStopped=false;}
  else{configureStopped();++setupCount;setupPcr=readPcr();knownStopped=setupPcr==stoppedPcr;if(!knownStopped)fail();}
 }
 if(o==1156){endPcr=readPcr();if(active||!knownStopped||endPcr!=stoppedPcr||setupCount!=1){fail();knownStopped=false;}}
 inWindow=o>=900&&o<1028;
}
inline void endFrame(){if(selected&&inWindow&&(frameCalls!=1||active||!knownStopped))fail();inWindow=false;}
inline void cleanup(){cleanupPcr=readPcr();cleanupVerified=!active&&knownStopped&&cleanupPcr==stoppedPcr;if(!cleanupVerified)fail();}
struct SceneScope {
 bool entered=false,sampled=false;U frame=0,scene=0,i0=0,d0=0;const int* ownerPtr=nullptr;
 explicit SceneScope(const int& owner){
  if(!selected||!inWindow||failed)return;
  if(active||frameCalls!=0||owner<0){fail();return;}
  ++frameCalls;++phaseCalls;
  const U actualPcr=readPcr();++commonControlReads;
  if(!knownStopped||actualPcr!=stoppedPcr){fail();knownStopped=false;return;}
  frame=NightSampler::frame;scene=static_cast<U>(owner);ownerPtr=&owner;
  activeFrame=frame;activeScene=scene;active=true;knownStopped=false;
  resetEnable();++resets;++enables;entered=true;sampled=enabled;
  if(sampled){i0=readI();d0=readD();phaseReads+=2;}
 }
 void stop(){
  if(!entered)return;
  U i1=0,d1=0;if(sampled){i1=readI();d1=readD();phaseReads+=2;}
  const bool contextStable=NightSampler::frame==frame&&*ownerPtr==static_cast<int>(scene);
  if(!contextStable)fail();
  const U actualPcr=readPcr();++commonControlReads;
  // PMU owner identity is independent of the mutable game context. A changed
  // game context is rejected, while this still-owned PMU is stopped safely.
  const bool pmuOwned=active&&activeFrame==frame&&activeScene==scene;
  if(!pmuOwned||actualPcr!=activePcr){fail();active=false;knownStopped=false;entered=false;return;}
  stopOwned();++stops;const U stopped=readPcr();++commonControlReads;
  active=false;knownStopped=stopped==stoppedPcr;if(!knownStopped)fail();
  if(sampled){
   if(!contextStable||!knownStopped||rowCount>=128||i1<i0||d1<d0||((i0|d0|i1|d1)&0x80000000U))fail();
   else rows[rowCount++]={frame,scene,i0,d0,i1,d1,4};
  }
  entered=false;
 }
 SceneScope(const SceneScope&)=delete;SceneScope& operator=(const SceneScope&)=delete;
 ~SceneScope(){stop();}
};
}

namespace NightPMU {inline U fakePcr=0,fakeI=100,fakeD=200,configureWrites=0,resetWrites=0,enableWrites=0,stopWrites=0;
 inline bool resetWhileEnabled=false;inline std::vector<char> trace;
 U hostReadPcr(){trace.push_back('P');return fakePcr;}
 U hostReadI(){trace.push_back('I');return fakeI++;}
 U hostReadD(){trace.push_back('D');return fakeD++;}
 void hostConfigureStopped(){++configureWrites;trace.push_back('C');fakePcr=stoppedPcr;}
 void hostResetEnable(){++resetWrites;++enableWrites;trace.push_back('R');if(fakePcr&0x80000000U)resetWhileEnabled=true;fakeI=fakeD=0;fakePcr=activePcr;trace.push_back('E');}
 void hostStopOwned(){++stopWrites;trace.push_back('S');fakePcr=stoppedPcr;}
}
static void reset(){using namespace NightPMU;selected=true;enabled=inWindow=active=knownStopped=cleanupVerified=failed=false;phase=frameCalls=phaseCalls=phaseReads=rowCount=commonControlReads=resets=enables=stops=beforePcr=preSetupPcr=setupPcr=endPcr=setupCount=activeFrame=activeScene=cleanupPcr=0;fakePcr=configureWrites=resetWrites=enableWrites=stopWrites=0;fakeI=100;fakeD=200;resetWhileEnabled=false;trace.clear();NightAblation::valid=true;}
static void initialize(bool on){NightPMU::begin(0,on);NightPMU::begin(750,on);NightPMU::begin(799,on);}
int main(){using namespace NightPMU;int owner=0;
 for(bool on:{false,true}){reset();initialize(on);if(!NightAblation::valid||fakePcr!=stoppedPcr||configureWrites!=1||resetWrites||enableWrites||stopWrites)return 10;
  trace.clear();for(U i=0;i<128;++i){begin(900+i,on);NightSampler::frame=900+i;{SceneScope a(owner);fakeI+=7;fakeD+=11;}endFrame();if(fakePcr!=stoppedPcr||active||!knownStopped)return 11;}
  if(!NightAblation::valid||phaseCalls!=128||commonControlReads!=384||resets!=128||enables!=128||stops!=128||phaseReads!=(on?512U:0U)||rowCount!=(on?128U:0U)||resetWhileEnabled)return 12;
  std::vector<char> single=on?std::vector<char>({'P','R','E','I','D','I','D','P','S','P'}):std::vector<char>({'P','R','E','P','S','P'});if(trace.size()!=128*single.size())return 13;for(U i=0;i<128;++i)for(U j=0;j<single.size();++j)if(trace[i*single.size()+j]!=single[j])return 14;
  begin(1156,on);cleanup();if(!NightAblation::valid||!cleanupVerified||fakePcr!=stoppedPcr||stopWrites!=128)return 15;
 }
 reset();initialize(true);begin(900,true);NightSampler::frame=900;auto early=[&](){SceneScope a(owner);return;};early();endFrame();if(!NightAblation::valid||stops!=1||active||fakePcr!=stoppedPcr)return 16;
 // Unknown active setup and entry owners are never reset/stopped/reconfigured.
 for(U prior:{0x80000001U,activePcr}){reset();fakePcr=prior;initialize(true);if(NightAblation::valid||configureWrites||resetWrites||enableWrites||stopWrites||fakePcr!=prior)return 20;}
 for(U prior:{0x80000001U,activePcr,0x00000001U}){reset();initialize(true);fakePcr=prior;begin(900,true);NightSampler::frame=900;{SceneScope a(owner);}endFrame();if(NightAblation::valid||resetWrites||enableWrites||stopWrites||fakePcr!=prior)return 21;}
 // Unknown owner at pre-stop receives no stop write; primary lifetime safety
 // remains unresolved in this failure path, and this host case is not safety proof.
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);fakePcr=0x80000001U;}endFrame();if(NightAblation::valid||stopWrites||fakePcr!=0x80000001U)return 22;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);SceneScope nested(owner);}endFrame();if(NightAblation::valid||resetWrites!=1||stopWrites!=1)return 23;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);owner=1;}owner=0;endFrame();if(NightAblation::valid||stopWrites!=1||fakePcr!=stoppedPcr)return 24;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);NightSampler::frame=901;}endFrame();if(NightAblation::valid||stopWrites!=1||fakePcr!=stoppedPcr)return 25;
 reset();initialize(true);begin(900,true);endFrame();if(NightAblation::valid||resetWrites||stopWrites)return 26;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);fakeI=0x80000000U;}endFrame();if(NightAblation::valid||stopWrites!=1)return 27;
 reset();initialize(true);fakePcr=activePcr;begin(1156,true);if(NightAblation::valid||stopWrites||fakePcr!=activePcr)return 28;
 reset();initialize(true);fakePcr=0x80000001U;cleanup();if(NightAblation::valid||stopWrites||fakePcr!=0x80000001U)return 29;
 if(resetWhileEnabled)return 30;
 // Permanent failure latch prevents every subsequent sample or phase rearm.
 for(int cause:{0,1,2}){reset();initialize(true);begin(900,true);NightSampler::frame=900;
  {SceneScope a(owner);if(cause==0)fakeI=0x80000000U;else if(cause==1)owner=1;else fakePcr=0x80000001U;}
  owner=0;endFrame();if(!failed||NightAblation::valid)return 40+cause;
  U oldConfigure=configureWrites,oldReset=resetWrites,oldEnable=enableWrites,oldStop=stopWrites;fakePcr=stoppedPcr;
  begin(901,true);NightSampler::frame=901;{SceneScope next(owner);}endFrame();
  begin(1800,true);begin(2599,true);
  if(!failed||configureWrites!=oldConfigure||resetWrites!=oldReset||enableWrites!=oldEnable||stopWrites!=oldStop)return 43+cause;
 }
 std::puts("PASS_HOST_PMU_V2_ORDER_GUARDS_ONLY_NO_RUNTIME_SAFETY_CLAIM");return 0;}
