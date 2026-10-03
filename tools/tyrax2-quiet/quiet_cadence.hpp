#pragma once
// PRIVATE TEMPLATE. Ordinary engine/game clocks are never modified.
#include <stdint.h>
namespace QuietCadence {
using U=uint32_t;using W=uint64_t;
constexpr U phases=3,phaseFrames=1800,taxFirst=800,chunkFrames=64,chunkCount=5,sampleFirst=900,sampleCount=128;
enum class PresentKind:U {Synchronous=0,Pipeline=1,Synthetic=2};
struct Present {U total=0,rendered=0,synthetic=0,last=0,lastRendered=0,kind=0,sequence=0,context=0xffffffffU;};
struct Sample {U frame,entryPeriod,wholeLoop,pacing,totalFlips,renderedFlips,syntheticFlips,presentPeriod,renderedPeriod,lastKind,sequence,context;};
struct Chunk {U first=0,loops=0,elapsed=0,pacing=0,totalFlips=0,renderedFlips=0,syntheticFlips=0;};
inline Present present{};
inline Sample samples[sampleCount]{};
inline Chunk chunks[phases][chunkCount]{};
static_assert(sizeof(samples)+sizeof(chunks)<=8192,"private common sample/chunk RAM exceeds8KiB");
inline U order=0,phase=0,frame=0,expectedIndex=0,counts[phases]{},chunkCounts[phases]{};
inline bool exported[phases]{};
inline U start=0,stallStart=0,previousEntry=0,chunkStart=0,chunkStall=0;
inline Present atStart{},previousPresent{},chunkPresent{};
inline bool valid=true,enabled=false,sampling=false,primed=false,chunkPrimed=false;
#ifdef PRIVATE_QUIET_HOST
U hostTicks();inline U reads=0;inline U ticks(){++reads;return hostTicks();}
#else
inline U ticks(){U value;asm volatile("mfc0 %0, $9":"=r"(value)::"memory");return value;}
#endif
inline bool deltaValid(U value){return value<0x80000000U;}
inline bool mode(U p){return (p==1)!=(order==1);}
inline void init(U selectedOrder){order=selectedOrder;expectedIndex=0;valid=selectedOrder<=1;present=Present{};primed=chunkPrimed=false;for(U p=0;p<phases;++p){exported[p]=false;counts[p]=chunkCounts[p]=0;for(auto& c:chunks[p])c=Chunk{};}for(auto& s:samples)s=Sample{};}
inline void markExported(U p){if(p>=phases||exported[p]||chunkCounts[p]!=chunkCount||counts[p]!=(mode(p)?sampleCount:0))valid=false;else exported[p]=true;}
inline bool complete(){if(expectedIndex<phases*phaseFrames)return false;for(U p=0;p<phases;++p)if(!exported[p])return false;return valid;}
// Reuse an EXISTING t1 immediately after actual flipBuffers returns.
inline void onPresent(U existingStamp,PresentKind kind,U sequence,U context){
 const U k=static_cast<U>(kind);if(k>2){valid=false;return;}
 ++present.total;present.last=existingStamp;present.kind=k;present.sequence=sequence;present.context=context;
 if(kind==PresentKind::Synthetic)++present.synthetic;else{++present.rendered;present.lastRendered=existingStamp;}
}
// Engine entry BEFORE Pad. Caller supplies actual current engine-loop index.
inline void beginLoop(U index,U cumulativePacing){
 if(index!=expectedIndex)valid=false;
 ++expectedIndex;
 frame=index;phase=index/phaseFrames;const U offset=index%phaseFrames;sampling=false;
 if(phase>=phases)return;
 enabled=mode(phase);
 if(offset==0){if(phase&&!exported[phase-1])valid=false;primed=chunkPrimed=false;}
 const bool boundary=offset>=taxFirst&&offset<=taxFirst+chunkFrames*chunkCount&&(offset-taxFirst)%chunkFrames==0;
 const bool readSample=enabled&&offset>=sampleFirst-1&&offset<sampleFirst+sampleCount;
 if(!boundary&&!readSample)return;
 const U now=ticks();
 if(boundary){
  const U b=(offset-taxFirst)/chunkFrames;
  if(b==0){chunkPrimed=true;}else if(!chunkPrimed){valid=false;}else{
   Chunk& c=chunks[phase][b-1];c={index-chunkFrames,chunkFrames,now-chunkStart,cumulativePacing-chunkStall,present.total-chunkPresent.total,present.rendered-chunkPresent.rendered,present.synthetic-chunkPresent.synthetic};
   if(!deltaValid(c.elapsed)||!deltaValid(c.pacing)||c.pacing>c.elapsed||c.totalFlips!=c.renderedFlips+c.syntheticFlips)valid=false;
   ++chunkCounts[phase];
  }
  chunkStart=now;chunkStall=cumulativePacing;chunkPresent=present;
 }
 if(readSample){start=now;stallStart=cumulativePacing;atStart=present;sampling=true;}
}
// Engine final statement AFTER Info/SifRpcGuard. No reads/writes to sample OFF.
inline void endLoop(U cumulativePacing){
 if(!sampling)return;
 const U end=ticks(),offset=frame%phaseFrames;
 if(offset==sampleFirst-1){primed=true;previousEntry=start;previousPresent=present;sampling=false;return;}
 const U i=offset-sampleFirst;
 if(!primed||i>=sampleCount||counts[phase]!=i){valid=false;sampling=false;return;}
 Sample& s=samples[i];s={frame,start-previousEntry,end-start,cumulativePacing-stallStart,present.total-atStart.total,present.rendered-atStart.rendered,present.synthetic-atStart.synthetic,present.last-previousPresent.last,present.lastRendered-previousPresent.lastRendered,present.kind,present.sequence,present.context};
 if(!deltaValid(s.entryPeriod)||!deltaValid(s.wholeLoop)||!deltaValid(s.pacing)||s.pacing>s.wholeLoop||s.totalFlips!=s.renderedFlips+s.syntheticFlips)valid=false;
 // Zero/multiple presentations are recorded and kept; periods are eligible
 // only if corresponding counter delta is exactly one. No hidden new waits.
 ++counts[phase];previousEntry=start;previousPresent=present;sampling=false;
}
}
