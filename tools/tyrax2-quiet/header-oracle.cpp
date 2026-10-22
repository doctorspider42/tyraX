#define PRIVATE_QUIET_HOST 1
#include <cassert>
#include <cstdio>
#include "quiet_cadence.hpp"
namespace QuietCadence {U clockValue=0;U hostTicks(){return clockValue;}}
using namespace QuietCadence;
static void schedule(U selected,bool mixed){
 init(selected);reads=0;U stall=0,phaseReadStart=0;auto* buffer=samples;
 for(U i=0;i<phaseFrames*phases;++i){U offset=i%phaseFrames,p=i/phaseFrames;if(offset==0)phaseReadStart=reads;
  clockValue=0xf0000000U+i*10000;beginLoop(i,stall);
  clockValue+=8000;stall+=1000;
  if(!mixed||i%17!=0){onPresent(clockValue,PresentKind::Pipeline,i+1,1);if(mixed&&i%19==0)onPresent(clockValue+100,PresentKind::Synthetic,0,1);}
  clockValue+=1000;endLoop(stall);assert(samples==buffer&&valid);
  if(offset==1327){assert(chunkCounts[p]==5);assert(counts[p]==(mode(p)?128:0));assert(reads-phaseReadStart==(mode(p)?262:6));markExported(p);}
 }
 assert(complete());for(U p=0;p<3;++p)for(auto c:chunks[p]){assert(c.elapsed==640000&&c.pacing==64000&&c.totalFlips==c.renderedFlips+c.syntheticFlips);}
 if(!mixed)for(auto s:samples){assert(s.entryPeriod==10000&&s.wholeLoop==9000&&s.pacing==1000&&s.totalFlips==1&&s.renderedFlips==1&&s.syntheticFlips==0&&s.presentPeriod==10000&&s.renderedPeriod==10000);}
}
int main(){
 schedule(0,false);schedule(1,false);schedule(0,true);schedule(1,true);
 init(2);assert(!valid);
 init(0);beginLoop(1,0);assert(!valid);
 init(0);markExported(0);assert(!valid);
 init(0);expectedIndex=1800;beginLoop(1800,0);assert(!valid);
 init(0);onPresent(1,static_cast<PresentKind>(3),0,0);assert(!valid);
 init(0);expectedIndex=800;clockValue=0;beginLoop(800,0);expectedIndex=864;clockValue=0x80000000U;beginLoop(864,0);assert(!valid);
 init(0);expectedIndex=800;clockValue=0;beginLoop(800,0);expectedIndex=864;clockValue=1000;beginLoop(864,1001);assert(!valid);
 init(1);expectedIndex=899;clockValue=0;beginLoop(899,0);clockValue=10;endLoop(0);expectedIndex=900;clockValue=20;beginLoop(900,0);clockValue=30;endLoop(11);assert(!valid);
 init(1);expectedIndex=900;clockValue=1;beginLoop(900,0);clockValue=2;endLoop(0);assert(!valid);
 init(1);expectedIndex=899;clockValue=0;beginLoop(899,0);clockValue=10;endLoop(0);expectedIndex=900;clockValue=20;beginLoop(900,0);clockValue=20+0x80000000U;endLoop(0);assert(!valid);
 std::printf("PASS actualQuietHeader schedules4x5400 OFFreads6 ONreads262 sampleBytes%zu chunkBytes%zu zeroMultipleSyntheticRecorded1 negatives10\n",sizeof(samples),sizeof(chunks));
}
