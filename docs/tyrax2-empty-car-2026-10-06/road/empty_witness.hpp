#pragma once
#include "debug/night_sampler.hpp"
#include <stdio.h>
namespace EmptyWitness {
inline bool active=false;inline unsigned frame=0,bags=0,vertices=0,billboards=0,roadBags=0,roadVertices=0,roadCount=0;
inline const void* roadPointers[128]{};
inline void roadCandidate(const void* bag){if(roadCount<128)roadPointers[roadCount++]=bag;else NightSampler::valid=false;}
inline void begin(unsigned f){frame=f;active=f<5400&&(f%1800==750||f%1800==1155);if(active)bags=vertices=billboards=roadBags=roadVertices=roadCount=0;}
inline void accepted(const void* bag,unsigned n,bool billboard){if(active){++bags;vertices+=n;billboards+=billboard;for(unsigned i=0;i<roadCount;++i)if(roadPointers[i]==bag){++roadBags;roadVertices+=n;break;}}}
inline void finish(){if(!active)return;if(!bags||!vertices||billboards||!roadBags||!roadVertices||bags<=roadBags||vertices<=roadVertices)NightSampler::valid=false;
printf("LOG: EMPTYRENDER phase=%u offset=%u acceptedBags=%u acceptedVertices=%u billboards=%u roadBags=%u roadVertices=%u carBags=%u carVertices=%u valid=%u\n",frame/1800,frame%1800,bags,vertices,billboards,roadBags,roadVertices,bags-roadBags,vertices-roadVertices,unsigned(NightSampler::valid));fflush(stdout);active=false;}
}
