#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace Tyra {
struct StaPipRetainedEntry { uintptr_t bboxVersion=0;uintptr_t colors=0;uintptr_t contentVersion=0;uintptr_t count=0;uintptr_t depthScaleBits=0;uintptr_t maxVertCount=0;uintptr_t normals=0;uintptr_t poolGeneration=0;uintptr_t poolLayout=0;uintptr_t primKey=0;uintptr_t program=0;uintptr_t programAddr=0;uintptr_t singleColor=0;uintptr_t stripped=0;uintptr_t sts=0;uintptr_t vertices=0; };
using StaPipBakedEntry=StaPipRetainedEntry;
struct StaPipRetainedCommands{static bool keyMatches(const StaPipRetainedEntry&,const StaPipRetainedEntry&);};
struct StaPipBakedStreams{static bool keyMatches(const StaPipBakedEntry&,const StaPipBakedEntry&);};
#include "actual-keys.inc"
}
int main(){using namespace Tyra;unsigned checks=0;auto check=[&](bool v){++checks;if(!v)std::abort();};StaPipBakedEntry a,b;check(StaPipBakedStreams::keyMatches(a,b));check(StaPipRetainedCommands::keyMatches(a,b));
b=a;b.bboxVersion=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.colors=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.contentVersion=1;check(!StaPipBakedStreams::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.count=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.depthScaleBits=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.maxVertCount=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.normals=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.poolGeneration=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.poolLayout=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.primKey=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.program=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.programAddr=1;check(!StaPipBakedStreams::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.singleColor=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.stripped=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.sts=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
b=a;b.vertices=1;check(!StaPipBakedStreams::keyMatches(a,b));check(!StaPipRetainedCommands::keyMatches(a,b));b=a;check(StaPipBakedStreams::keyMatches(a,b));
a.poolLayout=1;b=a;b.poolLayout=3;check(!StaPipBakedStreams::keyMatches(a,b));a.poolGeneration=7;b=a;b.poolGeneration=8;check(!StaPipBakedStreams::keyMatches(a,b));std::printf("PASS exact extracted cache keys checks=%u\n",checks);}
