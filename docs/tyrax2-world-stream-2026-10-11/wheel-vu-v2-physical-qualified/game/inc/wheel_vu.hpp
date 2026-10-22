#pragma once
#include <tyra>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include "renderer/core/paths/path1/vif1_queue.hpp"
namespace WheelVu {
inline bool active=false;
inline unsigned cars=0,draws=0,vertices=0;
struct Source {
  std::vector<Tyra::Vec4> positions,sts;
  std::vector<Tyra::Color> colors;
  unsigned run=0;u32 version=1,bbox=0;
};
inline std::map<int,std::unique_ptr<Source>> sources;
inline unsigned nextBox=0x70000000U;
inline void clear(){Tyra::Vif1Queue::drain();sources.clear();}
inline void reset(){cars=draws=vertices=0;}
inline Source& source(int id,const std::vector<float>& data,unsigned count,unsigned run){
  auto& entry=sources[id];
  if(!entry){
    entry.reset(new Source);entry->run=run;entry->bbox=++nextBox;
    entry->positions.reserve(count);entry->sts.reserve(count);entry->colors.reserve(count);
    const unsigned real=data.size()/8;
    TYRA_ASSERT(real && count>=real,"Wheel local source shape invalid");
    for(unsigned i=0;i<count;++i){
      const float* q=&data[(i<real?i:real-1)*8];
      entry->positions.emplace_back(q[0],q[1],q[2],1);
      entry->sts.emplace_back(q[6],q[7],1,0);
      entry->colors.emplace_back(128,128,128,128);
    }
  }
  TYRA_ASSERT(entry->positions.size()==count && entry->run==run,"Wheel local source changed without reset");
  return *entry;
}
inline void submit(Tyra::StaticPipeline& pipe,Source& source,Tyra::M4x4& matrix,
                   const Tyra::StaPipInfoBag& shape,Tyra::Texture* texture,
                   Tyra::StaPipLightReception reception){
  Tyra::StaPipInfoBag info=shape;info.model=&matrix;
  Tyra::StaPipColorBag color;color.many=source.colors.data();color.single=nullptr;
  color.contentVersion=&source.version;
  Tyra::StaPipTextureBag tex;tex.texture=texture;tex.coordinates=source.sts.data();
  tex.contentVersion=&source.version;
  Tyra::StaPipBag bag;bag.info=&info;bag.color=&color;bag.texture=texture?&tex:nullptr;
  bag.lighting=nullptr;bag.vertices=source.positions.data();bag.count=source.positions.size();
  bag.contentVersion=&source.version;bag.bboxVersion=source.bbox;
  bag.stripped=source.run!=0;bag.packageSize=source.run;
  pipe.core.render(&bag,reception);++draws;vertices+=bag.count;
}
inline void witness(unsigned phase,int arm=-1){
  std::printf("WHEELVU phase=%u arm=%d active=%u cars=%u draws=%u vertices=%u\n",
    phase,arm,unsigned(active),cars,draws,vertices);std::fflush(stdout);
}
}
