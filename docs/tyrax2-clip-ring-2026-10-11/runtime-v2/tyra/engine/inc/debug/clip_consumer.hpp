#pragma once
// Cold-only, topology-independent ordered triangle input oracle for A29.
namespace ClipConsumer {
inline bool observe=false;
inline unsigned headerChecks=0,hash=2166136261U,calls=0,verts=0;
inline unsigned nativeCalls=0,nativeTriangles=0;
inline void reset(){hash=2166136261U;calls=verts=headerChecks=nativeCalls=nativeTriangles=0;}
inline void bytes(const void* p,unsigned n){
  const auto* b=static_cast<const unsigned char*>(p);
  while(n--)hash=(hash^*b++)*16777619U;
}
inline void word(unsigned n){bytes(&n,sizeof(n));}
template<class Buffer> inline void input(Buffer* b){
  if(!observe)return;
  const unsigned n=b->nativeClipStrip?b->size-2:b->size/3;
  const unsigned streams=(b->bag->texture?1:0)|(b->bag->color->many?2:0)|(b->bag->lighting?4:0);
  calls+=n;verts+=n*3;
  if(b->nativeClipStrip){++nativeCalls;nativeTriangles+=n;}
  for(unsigned t=0;t<n;++t){
    word(0xA29);word(b->clipPlaneMask);word(streams);
    for(unsigned corner=0;corner<3;++corner){
      const unsigned local=b->nativeClipStrip && ((t+b->nativeClipParity)&1U) && corner<2
          ?1U-corner:corner;
      const unsigned i=b->nativeClipStrip?t+local:t*3+corner;
      bytes(b->vertices+i,sizeof(*b->vertices));
      if(streams&1)bytes(b->sts+i,sizeof(*b->sts));
      if(streams&2)bytes(b->colors+i,sizeof(*b->colors));
      if(streams&4)bytes(b->normals+i,sizeof(*b->normals));
    }
  }
}
}
