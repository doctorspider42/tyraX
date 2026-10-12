#pragma once
#include "debug/clip_capacity.hpp"
namespace ClipConsumer {
inline bool observe=false;
inline unsigned headerChecks=0;
inline unsigned hash=2166136261U,calls=0,verts=0;
inline void reset(){hash=2166136261U;calls=verts=headerChecks=0;}
inline void bytes(const void* p,unsigned n){const auto* b=static_cast<const unsigned char*>(p);while(n--)hash=(hash^*b++)*16777619U;}
inline void word(unsigned n){bytes(&n,sizeof(n));}
template<class Buffer> inline void input(Buffer* b){
 if(!observe)return;
 ++calls;verts+=b->size;word(b->size);word(b->clipPlaneMask);word(b->stripped);
 const unsigned streams=(b->bag->texture?1:0)|(b->bag->color->many?2:0)|(b->bag->lighting?4:0);
 word(streams);bytes(b->vertices,b->size*sizeof(*b->vertices));
 if(streams&1)bytes(b->sts,b->size*sizeof(*b->sts));
 if(streams&2)bytes(b->colors,b->size*sizeof(*b->colors));
 if(streams&4)bytes(b->normals,b->size*sizeof(*b->normals));
}
}
