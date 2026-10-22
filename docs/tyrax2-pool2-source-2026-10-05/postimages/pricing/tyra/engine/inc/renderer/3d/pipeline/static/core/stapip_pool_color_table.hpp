#pragma once
#include <cstdint>
#include <cstring>
namespace Tyra { namespace ExperimentalPoolTable {
struct alignas(16) ColorBits { uint32_t word[4]; };
struct alignas(16) Slice { uint32_t descriptor[4]; ColorBits colors[2]; };
struct View { const ColorBits* colors=nullptr; uint32_t members=0,total=0,generation=0; };
struct Admission { bool inside, list, unlitTC, contiguous, currentGeneration; };
inline bool finiteNonnegative(uint32_t w) { return (w & 0x80000000u)==0 && (w & 0x7f800000u)!=0x7f800000u; }
// Caller owns immutable/current run metadata. Failure leaves output unchanged.
inline bool describe(const ColorBits* memberColors,uint32_t members,uint32_t total,
                     uint32_t offset,uint32_t count,const Admission& a,Slice& out) {
 if(!memberColors || !a.inside || !a.list || !a.unlitTC || !a.contiguous || !a.currentGeneration) return false;
 if(members==0 || members>16 || total!=members*96u || count==0 || count>75 || count%3 || offset%3 || offset>total || count>total-offset) return false;
 const uint32_t first=offset/96u,last=(offset+count-1)/96u;
 if(last>=members || last-first>1) return false;
 const ColorBits& c0=memberColors[first];const ColorBits& c1=memberColors[last];
 for(unsigned j=0;j<3;++j) if(!finiteNonnegative(c0.word[j]) || !finiteNonnegative(c1.word[j])) return false;
 if(c0.word[3]!=0x43000000u || c1.word[3]!=0x43000000u) return false; // exact alpha128
 Slice ready{};ready.descriptor[0]=first==last ? count : 96u-offset%96u;
 ready.descriptor[1]=count;ready.descriptor[2]=1;ready.colors[0]=c0;ready.colors[1]=c1;
 std::memcpy(&out,&ready,sizeof ready);return true;
}
inline uint32_t inputExtent(uint32_t count) {return 2+2*count+3;}
inline uint32_t totalExtent(uint32_t count) {return inputExtent(count)+9+3*count;}
// Oracle only: never call this scanning comparator in measured windows.
inline bool expansionMatches(const Slice& s,const void* baseline,uint32_t count) {
 if(!baseline || count==0 || count>75 || count%3 || s.descriptor[1]!=count || s.descriptor[2]!=1 || s.descriptor[3]!=0 || s.descriptor[0]==0 || s.descriptor[0]>count || s.descriptor[0]%3) return false;
 for(uint32_t i=0;i<count;++i) if(std::memcmp(static_cast<const unsigned char*>(baseline)+i*16u,&s.colors[i<s.descriptor[0]?0:1],16)!=0) return false;
 return true;
}
static_assert(sizeof(ColorBits)==16 && sizeof(Slice)==48,"Pool2 VIF layout");
} }
