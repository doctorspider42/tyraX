#include "debug/empty_witness.hpp"
#include <cassert>
int main(){using namespace EmptyWitness;static_assert(sizeof(unsigned long long)==8);static_assert(sizeof(buildingMasks)==512*8);static_assert(sizeof(buildingRefs)==50*4);
 NightSampler::valid=true;assert(buildingBit(0)==1ULL);assert(buildingBit(32)==(1ULL<<32));assert(buildingBit(49)==(1ULL<<49));assert(NightSampler::valid);
 assert(buildingBit(50)==0&&!NightSampler::valid);NightSampler::valid=true;assert(buildingBit(64)==0&&!NightSampler::valid);
 NightSampler::valid=true;begin(750);int bag;buildingCandidate(&bag,buildingBit(32)|buildingBit(49));accepted(&bag,1680,false);assert(buildingBags==1&&buildingVertices==1680&&buildingRefs[32]==1&&buildingRefs[49]==1&&buildingRefs[0]==0);
 begin(750);carrier=55;accepted(&bag,1152,false);assert(buildingRefs[49]==1&&buildingBags==1);assert(buildingRefs[32]==0);
 begin(750);carrier=38;accepted(&bag,1680,false);assert(buildingRefs[32]==1);
 begin(750);NightSampler::valid=true;buildingCandidate(&bag,1ULL<<50);assert(!NightSampler::valid&&buildingCount==0);
 begin(750);NightSampler::valid=true;buildingCandidate(&bag,0);assert(!NightSampler::valid&&buildingCount==0);
 begin(750);NightSampler::valid=true;for(unsigned i=0;i<512;++i)buildingCandidate(&bag,1ULL);assert(buildingCount==512&&NightSampler::valid);buildingCandidate(&bag,1ULL);assert(buildingCount==512&&!NightSampler::valid);
 begin(751);NightSampler::valid=true;accepted(&bag,999,false);assert(!active&&NightSampler::valid);return 0;}
