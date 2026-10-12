#pragma once
// Cold-only source/delivery contracts; ClipConsumer is supplied by the fixture.
namespace CoarseStrip {
inline bool active=false;
struct Digest {
  unsigned hash=2166136261U,triangles=0,vertices=0;
  void bytes(const void* p,unsigned n){
    const auto* b=static_cast<const unsigned char*>(p);
    while(n--)hash=(hash^*b++)*16777619U;
  }
  void word(unsigned n){bytes(&n,sizeof(n));}
};
inline Digest source,delivered,residual;
inline unsigned sourcePackages=0,deliveryJobs=0;
inline void reset(){source={};delivered={};residual={};sourcePackages=deliveryJobs=0;}
template<class Geometry> inline void ordered(Digest& d,const Geometry& b,
                                            bool strip,unsigned parity){
  const unsigned n=strip?b.size-2:b.size/3;
  const unsigned streams=(b.bag->texture?1:0)|(b.bag->color->many?2:0)|
                         (b.bag->lighting?4:0);
  d.triangles+=n;d.vertices+=3*n;
  for(unsigned t=0;t<n;++t){
    d.word(0xA30);d.word(b.clipPlaneMask);d.word(streams);
    for(unsigned corner=0;corner<3;++corner){
      const unsigned local=strip && ((t+parity)&1U) && corner<2?1U-corner:corner;
      const unsigned i=strip?t+local:3*t+corner;
      d.bytes(b.vertices+i,sizeof(*b.vertices));
      if(streams&1)d.bytes(b.sts+i,sizeof(*b.sts));
      if(streams&2)d.bytes(b.colors+i,sizeof(*b.colors));
      if(streams&4)d.bytes(b.normals+i,sizeof(*b.normals));
    }
  }
}
template<class Package> inline void expect(const Package& b,unsigned parity=0){
  if(!ClipConsumer::observe)return;
  ++sourcePackages;ordered(source,b,true,parity);
}
template<class Buffer> inline void input(const Buffer& b){
  if(!ClipConsumer::observe)return;
  if(b.coarseClipStrip){
    ++deliveryJobs;ordered(delivered,b,true,b.nativeClipParity);
  } else if(!b.partialStripOrigin){
    ordered(residual,b,b.nativeClipStrip,b.nativeClipParity);
  }
}
inline bool same(const Digest& a,const Digest& b){
  return a.hash==b.hash && a.triangles==b.triangles && a.vertices==b.vertices;
}
}
