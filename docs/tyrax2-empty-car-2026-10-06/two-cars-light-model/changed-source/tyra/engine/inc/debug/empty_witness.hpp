#pragma once
#include "debug/night_sampler.hpp"
#include <stdio.h>
namespace EmptyWitness {
inline bool active=false;inline unsigned frame=0,bags=0,vertices=0,billboards=0,roadBags=0,roadVertices=0,roadCount=0;
inline const void* roadPointers[128]{};
inline unsigned modelBags=0,modelVertices=0;
inline unsigned carrier=0,carrierBags[4]{},carrierVertices[4]{},selectedLights[4]{},effectiveLights[4]{};
inline void selected(bool good){if(active&&carrier>0&&carrier<=4&&good)++selectedLights[carrier-1];}
inline void effective(bool good){if(active&&carrier>0&&carrier<=4&&good)++effectiveLights[carrier-1];}
inline void roadCandidate(const void* bag){if(roadCount<128)roadPointers[roadCount++]=bag;else NightSampler::valid=false;}
inline void begin(unsigned f){frame=f;active=f<5400&&(f%1800==750||f%1800==1155);if(active){bags=vertices=billboards=roadBags=roadVertices=roadCount=carrier=modelBags=modelVertices=0;for(unsigned i=0;i<4;++i)carrierBags[i]=carrierVertices[i]=selectedLights[i]=effectiveLights[i]=0;}}
inline void accepted(const void* bag,unsigned n,bool billboard){if(active){++bags;vertices+=n;billboards+=billboard;if(carrier==5){++modelBags;modelVertices+=n;}if(carrier>0&&carrier<=4){++carrierBags[carrier-1];carrierVertices[carrier-1]+=n;}for(unsigned i=0;i<roadCount;++i)if(roadPointers[i]==bag){++roadBags;roadVertices+=n;break;}}}
inline void finish(){if(!active)return;if(!bags||!vertices||billboards||!roadBags||!roadVertices||!modelBags||!modelVertices||bags<=roadBags+modelBags||vertices<=roadVertices+modelVertices)NightSampler::valid=false;
for(unsigned i=0;i<4;++i)if(!carrierBags[i]||!carrierVertices[i]||!selectedLights[i]||!effectiveLights[i])NightSampler::valid=false;
printf("LOG: MODELRENDER phase=%u offset=%u owner=4 acceptedBags=%u acceptedVertices=%u valid=%u\n",frame/1800,frame%1800,modelBags,modelVertices,unsigned(NightSampler::valid));
printf("LOG: TWOCARLIGHT phase=%u offset=%u ravagerBodySelected=%u picaBodySelected=%u ravagerWheelSelected=%u picaWheelSelected=%u ravagerBodyEffective=%u picaBodyEffective=%u ravagerWheelEffective=%u picaWheelEffective=%u valid=%u\n",frame/1800,frame%1800,selectedLights[0],selectedLights[1],selectedLights[2],selectedLights[3],effectiveLights[0],effectiveLights[1],effectiveLights[2],effectiveLights[3],unsigned(NightSampler::valid));
printf("LOG: TWOCARRENDER phase=%u offset=%u ravagerBodyBags=%u ravagerBodyVertices=%u picaBodyBags=%u picaBodyVertices=%u ravagerWheelBags=%u ravagerWheelVertices=%u picaWheelBags=%u picaWheelVertices=%u valid=%u\n",frame/1800,frame%1800,carrierBags[0],carrierVertices[0],carrierBags[1],carrierVertices[1],carrierBags[2],carrierVertices[2],carrierBags[3],carrierVertices[3],unsigned(NightSampler::valid));
printf("LOG: EMPTYRENDER phase=%u offset=%u acceptedBags=%u acceptedVertices=%u billboards=%u roadBags=%u roadVertices=%u carBags=%u carVertices=%u valid=%u\n",frame/1800,frame%1800,bags,vertices,billboards,roadBags,roadVertices,bags-roadBags-modelBags,vertices-roadVertices-modelVertices,unsigned(NightSampler::valid));fflush(stdout);active=false;}
}
