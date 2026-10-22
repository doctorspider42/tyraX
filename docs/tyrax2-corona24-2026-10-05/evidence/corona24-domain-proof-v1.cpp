#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
// Exhaust all positive binary32 values in [1/16, 2^24).
// Below 1/16 FTOI4 returns zero; over this range the 24-bit GS scale is exceeded.
int main(){
 uint64_t inputs=0,large=0; uint32_t max=0;
 for(uint32_t b=0x3d800000;b<0x4b800000;++b){
  float f;std::memcpy(&f,&b,4);double scaled=double(f)*16.0;
  uint32_t z=uint32_t(scaled);float converted=float(z);
  assert(double(converted)==double(z));++inputs;large+=z>0x1000000; if(z>max)max=z;
 }
 // Arbitrary 28-bit integers are NOT safe: the source restriction is essential.
 assert(float(0x1000000u)==float(0x1000001u));
 std::printf("{\"status\":\"PASS_EXHAUSTIVE_REACHABLE_FTOI4_Z_ITOF0_EXACT\",\"binary32Inputs\":%llu,\"above24SignificantBitThreshold\":%llu,\"maxZ\":%u,\"arbitraryIntegerCollisionControl\":true}\n",(unsigned long long)inputs,(unsigned long long)large,max);
}
