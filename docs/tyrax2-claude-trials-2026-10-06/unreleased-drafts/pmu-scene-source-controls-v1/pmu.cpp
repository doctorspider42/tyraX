#include <stdint.h>
#include <vector>
#include <cstdio>
#define PRIVATE_PMU_HOST 1
namespace NightAblation {inline bool valid=true;}
namespace NightSampler {inline uint32_t frame=0;}
// Private whole-Scene PMU observer. Miss endpoints are provisional event data;
// no PMU overflow semantics, cache miss rates or elapsed stall cost is asserted.

namespace NightPMU {
using U=uint32_t;
constexpr U pcr=0x800340d0U;
struct Row {U frame,scene,i0,d0,i1,d1,reads;};
inline Row rows[128]{};
inline U phase=0,frameCalls=0,phaseCalls=0,phaseReads=0,rowCount=0;
inline U beforePcr=0,preSetupPcr=0,setupPcr=0,endPcr=0,setupCount=0;
inline bool selected=false,enabled=false,inWindow=false,active=false,knownOwned=false;
inline U preStopPcr=0,postStopPcr=0;
inline bool cleanupPerformed=false;
#ifdef PRIVATE_PMU_HOST
U hostReadPcr();U hostReadI();U hostReadD();void hostSetup();void hostStop();
inline U readPcr(){return hostReadPcr();}
inline U readI(){return hostReadI();}
inline U readD(){return hostReadD();}
inline void setup(){hostSetup();}
inline void stopCounters(){hostStop();}
#else
// mfps/mtps/mfpc/mtpc syntax comes from pinned ps2dev/binutils-gdb opcode table;
// setup and counter-read sync sequence from pinned ps2stuff core.cpp/core.h.
// Root must qualify actual native placement before release.
inline U readPcr(){U v;asm volatile(".set push\n.set noreorder\nmfps %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readI(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,0\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline U readD(){U v;asm volatile(".set push\n.set noreorder\nmfpc %0,1\nsync.p\n.set pop":"=r"(v)::"memory");return v;}
inline void setup(){const U v=pcr;asm volatile(".set push\n.set noreorder\nmtps $0,0\nsync.p\nmtpc $0,0\nmtpc $0,1\nsync.p\nmtps %0,0\nsync.p\n.set pop"::"r"(v):"memory");}
inline void stopCounters(){asm volatile(".set push\n.set noreorder\nmtps $0,0\nsync.p\n.set pop":::"memory");}
#endif
inline void fail(){NightAblation::valid=false;}
inline void begin(U index,bool on){
 const U p=index/1800,o=index%1800;inWindow=false;frameCalls=0;
 if(!selected||p>=3)return;
 if(o==0){if(active)fail();phase=p;enabled=on;phaseCalls=phaseReads=rowCount=setupCount=0;beforePcr=preSetupPcr=setupPcr=endPcr=0;for(auto& r:rows)r=Row{};}
 if(o==750)beforePcr=readPcr();
 if(o==799){preSetupPcr=readPcr();
  // A running unknown configuration belongs to somebody else. Never stop or
  // reset it. Same bits without a previous successful local setup is unknown.
  if((preSetupPcr&0x80000000U)&&(!knownOwned||preSetupPcr!=pcr)){fail();knownOwned=false;}
  else{setup();++setupCount;setupPcr=readPcr();knownOwned=setupPcr==pcr;if(!knownOwned)fail();}
 }
 if(o==1156){endPcr=readPcr();if(endPcr!=pcr||setupCount!=1){fail();knownOwned=false;}}
 inWindow=o>=900&&o<1028;
}
inline void endFrame(){if(selected&&inWindow&&(frameCalls!=1||active))fail();inWindow=false;}
inline void cleanup(){
 preStopPcr=readPcr();cleanupPerformed=false;
 if(knownOwned&&preStopPcr==pcr){stopCounters();cleanupPerformed=true;postStopPcr=readPcr();knownOwned=false;if(postStopPcr!=0)fail();}
 else{postStopPcr=preStopPcr;knownOwned=false;fail();}
}
struct SceneScope {
 bool entered=false,sampled=false;U frame=0,scene=0,i0=0,d0=0;const int* ownerPtr=nullptr;
 explicit SceneScope(const int& owner){
  if(!selected||!inWindow)return;
  if(active||frameCalls!=0||owner<0){fail();return;}
  ++frameCalls;++phaseCalls;entered=true;active=true;
  frame=NightSampler::frame;scene=static_cast<U>(owner);ownerPtr=&owner;
  sampled=enabled;
  if(sampled){i0=readI();d0=readD();phaseReads+=2;}
 }
 void stop(){
  if(!entered)return;
  if(!active||NightSampler::frame!=frame||*ownerPtr!=static_cast<int>(scene))fail();
  if(sampled){const U i1=readI(),d1=readD();phaseReads+=2;
   if(!active||NightSampler::frame!=frame||*ownerPtr!=static_cast<int>(scene)||rowCount>=128||i1<i0||d1<d0||((i0|d0|i1|d1)&0x80000000U)){fail();}
   else rows[rowCount++]={frame,scene,i0,d0,i1,d1,4};
  }
  active=false;entered=false;
 }
 ~SceneScope(){stop();}
};
}

namespace NightPMU {inline U fakePcr=0,fakeI=100,fakeD=200,setups=0,stops=0;inline std::vector<char> reads;
 U hostReadPcr(){reads.push_back('P');return fakePcr;}
 U hostReadI(){reads.push_back('I');return fakeI++;}
 U hostReadD(){reads.push_back('D');return fakeD++;}
 void hostSetup(){++setups;fakePcr=pcr;fakeI=fakeD=0;}
 void hostStop(){++stops;fakePcr=0;}
}
static void reset(){using namespace NightPMU;selected=true;enabled=inWindow=active=knownOwned=cleanupPerformed=false;phase=frameCalls=phaseCalls=phaseReads=rowCount=setupCount=beforePcr=preSetupPcr=setupPcr=endPcr=preStopPcr=postStopPcr=0;fakePcr=setups=stops=0;fakeI=100;fakeD=200;reads.clear();NightAblation::valid=true;}
int main(){using namespace NightPMU;int owner=0;reset();begin(0,true);begin(750,true);begin(799,true);if(setups!=1||setupPcr!=pcr||!knownOwned)return 10;
 reads.clear();begin(900,true);NightSampler::frame=900;{SceneScope a(owner);}endFrame();if(!NightAblation::valid||rowCount!=1||phaseReads!=4||reads!=std::vector<char>({'I','D','I','D'}))return 11;
 // Every early return still owns the exact destructor endpoint.
 begin(901,true);NightSampler::frame=901;auto early=[&](){SceneScope a(owner);return;};early();endFrame();if(rowCount!=2||active)return 12;
 begin(1800,false);begin(2599,false);reads.clear();begin(2700,false);NightSampler::frame=2700;{SceneScope a(owner);}endFrame();if(!NightAblation::valid||rowCount||phaseReads||!reads.empty()||phaseCalls!=1)return 13;
 // Unknown enabled configuration, including same bits, must never be stopped.
 for(U prior: {0x80000001U,pcr}){reset();fakePcr=prior;begin(0,true);begin(799,true);if(NightAblation::valid||setups!=0||fakePcr!=prior)return 20;}
 reset();begin(0,true);begin(799,true);begin(1156,true);if(!NightAblation::valid)return 21;
 fakePcr=0x80000001U;begin(1800,true);begin(2599,true);if(NightAblation::valid||setups!=1||fakePcr!=0x80000001U)return 22;
 reset();begin(0,true);begin(799,true);fakePcr=0;begin(1156,true);if(NightAblation::valid)return 23;
 reset();begin(0,true);begin(799,true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);SceneScope nested(owner);}endFrame();if(NightAblation::valid)return 24;
 reset();begin(0,true);begin(799,true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);owner=1;}owner=0;endFrame();if(NightAblation::valid)return 25;
 reset();begin(0,true);begin(799,true);begin(900,true);endFrame();if(NightAblation::valid)return 26;
 reset();begin(0,true);begin(799,true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);NightSampler::frame=901;}endFrame();if(NightAblation::valid)return 27;
 reset();begin(0,true);begin(799,true);fakeI=100;begin(900,true);NightSampler::frame=900;{SceneScope a(owner);fakeI=0;}endFrame();if(NightAblation::valid)return 28;
 reset();begin(0,true);begin(799,true);begin(900,true);NightSampler::frame=900;fakeI=0x80000000U;{SceneScope a(owner);}endFrame();if(NightAblation::valid)return 29;
 reset();begin(0,true);begin(799,true);cleanup();if(!NightAblation::valid||stops!=1||!cleanupPerformed||preStopPcr!=pcr||postStopPcr!=0||knownOwned)return 30;
 reset();begin(0,true);begin(799,true);fakePcr=0x80000001U;cleanup();if(NightAblation::valid||stops||cleanupPerformed||fakePcr!=0x80000001U)return 31;
 reset();fakePcr=pcr;cleanup();if(NightAblation::valid||stops||cleanupPerformed||fakePcr!=pcr)return 32;
 std::puts("PASS_HOST_PMU_SOURCE_GUARDS_AND_ORDER_ONLY");return 0;}
