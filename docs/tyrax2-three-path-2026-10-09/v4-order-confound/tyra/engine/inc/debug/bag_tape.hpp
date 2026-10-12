#pragma once
// Private A18 qualification harness, not a runtime renderer API.
#include "renderer/core/renderer_core.hpp"
#include "renderer/3d/pipeline/static/core/stapip_core.hpp"
#include <memory>
#include <vector>
#include <cstring>
#include <cstdio>
#include <screenshot.h>
namespace BagTape {
using namespace Tyra;
inline bool recording=false,replaying=false,frozen=false,control=false;
inline unsigned iteration=0,bytes=0,calls=0;
inline RendererCore* renderer=nullptr;
inline bool checking=false;
inline unsigned checkIndex=0,checkMismatch=0,checkMask=0,firstMismatch=~0u;
struct Draw {
  StaPipBag bag;
  StaPipInfoBag info;
  StaPipColorBag color;
  StaPipTextureBag texture;
  StaPipLightingBag lighting;
  StaPipBillboardBag billboard;
  PipelineDirLightsBag lights{true};
  M4x4 model,lightMatrix;
  Color single;
  Vec4 lightColors[4],lightDirections[3];
  u32 stamps[4]{};
  std::vector<Vec4> vertices,coords,normals;
  std::vector<Color> colors;
  StaPipCore* pipe=nullptr;
  StaPipLightReception reception=StaPipLightReception::FromInfo;
  RendererCore3D view;
  RendererCoreFog fog;
  RendererCoreSpotLight spot,dynamic[RendererCore::DYN_LIGHTS_MAX];
  unsigned dynamicCount=0;
  template<class T> void copy(std::vector<T>& dst,const T* src,unsigned n){
    TYRA_ASSERT(bytes+n*sizeof(T)<=5*1024*1024,"Bag tape stream budget exceeded");
    dst.resize(n);memcpy(dst.data(),src,n*sizeof(T));bytes+=n*sizeof(T);
  }
  const u32* stamp(const u32* p,unsigned i){if(!p)return nullptr;stamps[i]=*p;return &stamps[i];}
  void capture(StaPipCore* p,StaPipBag* b,StaPipLightReception r){
    TYRA_ASSERT(b && b->info && b->info->model && b->color,"Unsupported tape descriptor");
    pipe=p;reception=r;bag=*b;info=*b->info;model=*info.model;info.model=&model;bag.info=&info;
    color=*b->color;bag.color=&color;
    copy(vertices,b->vertices,b->count);bag.vertices=vertices.data();
    bag.contentVersion=stamp(b->contentVersion,0);
    if(color.single){single=*color.single;color.single=&single;}
    if(color.many){copy(colors,color.many,b->count);color.many=colors.data();}
    color.contentVersion=stamp(color.contentVersion,1);
    if(b->texture){texture=*b->texture;bag.texture=&texture;
      copy(coords,texture.coordinates,b->count);texture.coordinates=coords.data();
      texture.contentVersion=stamp(texture.contentVersion,2);}
    if(b->lighting){lighting=*b->lighting;bag.lighting=&lighting;
      lightMatrix=*lighting.lightMatrix;lighting.lightMatrix=&lightMatrix;
      copy(normals,lighting.normals,b->count);lighting.normals=normals.data();
      lighting.contentVersion=stamp(lighting.contentVersion,3);
      memcpy(lightColors,lighting.dirLights->getLightColors(),sizeof(lightColors));
      memcpy(lightDirections,lighting.dirLights->getLightDirections(),sizeof(lightDirections));
      lights.setLightsManually(lightColors,lightDirections);lights.signedSH=lighting.dirLights->signedSH;
      lighting.dirLights=&lights;}
    if(b->billboard){billboard=*b->billboard;bag.billboard=&billboard;}
    view=renderer->renderer3D;fog=renderer->fog;spot=renderer->spot;
    dynamicCount=renderer->dynLightCount;
    TYRA_ASSERT(dynamicCount<=RendererCore::DYN_LIGHTS_MAX,"Invalid light prefix");
    for(unsigned i=0;i<dynamicCount;++i)dynamic[i]=renderer->dynLights[i];
  }
  void run(){
    renderer->renderer3D=view;renderer->fog=fog;renderer->spot=spot;
    renderer->dynLightCount=dynamicCount;
    for(unsigned i=0;i<dynamicCount;++i)renderer->dynLights[i]=dynamic[i];
    pipe->render(&bag,reception);
  }
  unsigned compare(StaPipBag* b){
    unsigned mask=0;
    if(b->count!=bag.count)return 1;
    if(memcmp(vertices.data(),b->vertices,b->count*sizeof(Vec4)))mask|=2;
    if(memcmp(model.data,b->info->model->data,sizeof(model.data)))mask|=4;
    if(bool(b->color->many)!=bool(bag.color->many))mask|=8;
    else if(b->color->many && memcmp(colors.data(),b->color->many,b->count*sizeof(Color)))mask|=8;
    if(bool(b->texture)!=bool(bag.texture))mask|=16;
    else if(b->texture && memcmp(coords.data(),b->texture->coordinates,b->count*sizeof(Vec4)))mask|=16;
    if(memcmp(view.getViewProj().data,renderer->renderer3D.getViewProj().data,sizeof(model.data)))mask|=32;
    StaPipInfoBag live=*b->info;live.model=&model;
    if(memcmp(&live,&info,sizeof(info)))mask|=64;
    return mask;
  }
};
enum Kind { DrawBag,BeginBatch,EndBatch,BeginEnv,EndEnv };
struct Event {
  Kind kind;std::unique_ptr<Draw> draw;
  StaPipQBufferRenderer* batch=nullptr;
  RendererCoreEnvMap* env=nullptr;
  Color clear;
};
inline std::vector<std::unique_ptr<Event>> events;
inline Event& append(Kind kind){
  TYRA_ASSERT(events.size()<1024,"Bag tape event capacity exceeded");
  events.emplace_back(new Event);events.back()->kind=kind;return *events.back();
}
inline void record(StaPipCore* pipe,StaPipBag* bag,StaPipLightReception reception){
  if(checking && bag && bag->count){
    while(checkIndex<events.size() && events[checkIndex]->kind!=DrawBag)++checkIndex;
    TYRA_ASSERT(checkIndex<events.size(),"Extra source bag after replay");
    unsigned mask=events[checkIndex]->draw->compare(bag);
    if(mask){++checkMismatch;checkMask|=mask;if(firstMismatch==~0u)firstMismatch=checkIndex;}
    ++checkIndex;
  }
  if(!recording || !bag || !bag->count)return;
  auto& e=append(DrawBag);e.draw.reset(new Draw);e.draw->capture(pipe,bag,reception);++calls;
}
inline void batch(StaPipQBufferRenderer* p,bool begin){if(recording)append(begin?BeginBatch:EndBatch).batch=p;}
inline void env(RendererCoreEnvMap* p,bool begin,const Color& color=Color()){
  if(recording){auto& e=append(begin?BeginEnv:EndEnv);e.env=p;e.clear=color;}
}
inline void replay(){
  TYRA_ASSERT(!recording && !events.empty(),"No qualified tape");
  replaying=true;
  for(auto& ptr:events){auto& e=*ptr;
    switch(e.kind){
      case DrawBag:e.draw->run();break;
      case BeginBatch:e.batch->beginSubmissionBatch();break;
      case EndBatch:e.batch->endSubmissionBatch();break;
      case BeginEnv:e.env->begin(e.clear);break;
      case EndEnv:e.env->end();break;
    }
  }
  replaying=false;
}
inline void captureImage(RendererCore& core,unsigned stage,bool envImage){
  core.synchronizeFrame();core.sync.align2D();
  auto* fb=core.gs.getPreviousRealFrameBuffer();
  unsigned address=fb->address,width=fb->width,height=fb->height,psm=fb->psm;
  if(envImage){auto* t=core.envMap.getTexture()->vramResident;address=t->address;width=height=128;psm=t->psm;}
  TYRA_ASSERT((psm==0 || psm==2) && width<=1024,"Unsupported tape readback format");
  char path[80];snprintf(path,sizeof(path),"host:tape-%u-%s.bin",stage,envImage?"env":"main");
  FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"Tape readback open failed");
  unsigned h[4]={width,height,psm,stage};TYRA_ASSERT(fwrite(h,sizeof(h),1,f)==1,"Tape header write failed");
  alignas(16) static unsigned line[1024];
  for(unsigned y=0;y<height;++y){
    TYRA_ASSERT(ps2_screenshot(line,address/64,0,y,width,1,psm),"Tape GS readback failed");
    FlushCache(0);unsigned stride=psm==2?2:4;
    TYRA_ASSERT(fwrite(line,stride,width,f)==width,"Tape pixels write failed");
  }
  TYRA_ASSERT(fclose(f)==0,"Tape close failed");
}
}
