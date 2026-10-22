#define PRIVATE_QUIET_HOST 1
#include "quiet_runtime.hpp"
#include <cassert>
namespace QuietCadence {U hostTicks(){return 0;}}
int main(){unsigned value;const char* p="cfg-oracle.tmp";
 const char* good[]={"0","1\n","  1 \r\n","\t0\t"};for(auto s:good){FILE*f=fopen(p,"w");fputs(s,f);fclose(f);assert(QuietRuntime::readFlag(p,value));}
 const char* bad[]={"","2","-1","01","1 garbage","0\n1","1\0junk"};for(unsigned i=0;i<6;++i){FILE*f=fopen(p,"w");fputs(bad[i],f);fclose(f);assert(!QuietRuntime::readFlag(p,value));}
 {FILE*f=fopen(p,"wb");const char data[]={'1',0,'j'};fwrite(data,1,3,f);fclose(f);assert(!QuietRuntime::readFlag(p,value));}
 remove(p);assert(!QuietRuntime::readFlag(p,value));assert(QuietRuntime::readFlag(p,value,true,1)&&value==1);puts("PASS strict cfg4positive7negative requiredMissing optionalDefault");
}
