#pragma once
#include <cstdio>
#include <cstring>
#include <vector>
#include "renderer/core/renderer_core.hpp"
#include "renderer/3d/pipeline/static/core/bag/stapip_bag.hpp"

namespace WorldSourceCapture {
inline bool active=false,valid=true;
inline unsigned stage=0,records=0;
inline std::vector<unsigned char> bytes;
constexpr unsigned limit=2*1024*1024;
inline bool append(const void* data,unsigned size){
  if(!valid)return false;
  if(size>limit-bytes.size() || (size && !data)){valid=false;active=false;return false;}
  const auto* first=static_cast<const unsigned char*>(data);
  if(size)bytes.insert(bytes.end(),first,first+size);
  return true;
}
inline void field(unsigned kind,const void* data,unsigned size){
  const unsigned header[]={kind,size};
  append(header,sizeof(header));append(data,size);
}
inline void begin(unsigned value){
  active=false;valid=true;bytes.clear();records=0;stage=value;
  try{bytes.reserve(limit);}catch(...){valid=false;return;}
  active=true;
}
inline void capture(int owner,Tyra::StaPipBag* b,Tyra::RendererCore& core,bool receives){
  if(!active)return;
  if(!b || !b->info || !b->info->model || !b->color || !b->vertices ||
     b->count>limit/16){valid=false;active=false;return;}
  const unsigned start=bytes.size(),placeholder=0;
  append(&placeholder,4);
  const auto& i=*b->info;
  const unsigned meta[]={unsigned(owner),b->count,unsigned(b->stripped),b->packageSize,
    unsigned(i.shadingType),unsigned(i.textureMappingType),unsigned(i.transformationType),
    unsigned(i.blendingEnabled),unsigned(i.antiAliasingEnabled),unsigned(i.zTestType),
    unsigned(i.frustumCulling),unsigned(i.fogDisabled),i.additiveBlendFix,i.subtractiveBlendFix,
    unsigned(i.dynLightPick),unsigned(i.dynamicLightReceive),unsigned(i.dynLightSkipSlot),
    unsigned(i.spotLit),unsigned(i.dateLit),unsigned(i.blssProxy),unsigned(i.fullClipChecks),
    unsigned(receives),unsigned(b->billboard!=nullptr),unsigned(b->lighting!=nullptr),
    b->texture && b->texture->texture?b->texture->texture->id:0,
    b->texture?unsigned(b->texture->textureFunction):0,
    b->texture?unsigned(b->texture->coordinatesAreNormals):0};
  field(1,meta,sizeof(meta));
  field(2,core.renderer3D.getViewProj().data,64);field(3,i.model->data,64);
  field(4,b->vertices,b->count*16);
  if(b->color->many)field(5,b->color->many,b->count*16);
  if(b->color->single)field(6,b->color->single,16);
  if(b->texture){
    field(7,b->texture->coordinates,b->count*16);
    const Tyra::Vec4 basis[]={b->texture->envRight,b->texture->envUp};
    if(b->texture->coordinatesAreNormals)field(12,basis,sizeof(basis));
    if(auto* texture=b->texture->texture){
      field(13,texture->name.data(),texture->name.size());
      const auto& w=*texture->getWrapSettings();
      const unsigned wrap[]={w.horizontal,w.vertical,w.minu,w.maxu,w.minv,w.maxv};
      field(14,wrap,sizeof(wrap));
    }
  }
  if(b->lighting){
    const auto& l=*b->lighting;
    field(8,l.normals,b->count*16);field(9,l.lightMatrix?l.lightMatrix->data:nullptr,64);
    if(l.dirLights){
      field(10,l.dirLights->getLightColors(),64);
      field(11,l.dirLights->getLightDirections(),48);
    }else{valid=false;active=false;}
  }
  unsigned fog[]={unsigned(core.fog.enabled),0,0};
  memcpy(fog+1,&core.fog.scale,4);memcpy(fog+2,&core.fog.offset,4);field(15,fog,sizeof(fog));
  const unsigned stamps[]={b->bboxVersion,b->contentVersion?*b->contentVersion:0,
    b->color->contentVersion?*b->color->contentVersion:0,
    b->texture && b->texture->contentVersion?*b->texture->contentVersion:0};
  field(16,stamps,sizeof(stamps));
  if(valid){const unsigned length=bytes.size()-start-4;memcpy(bytes.data()+start,&length,4);++records;}
}
inline void finish(){
  active=false;
  if(!valid){printf("A44SOURCE stage=%u valid=0 bytes=%u\n",stage,unsigned(bytes.size()));return;}
  char path[80];snprintf(path,sizeof(path),"host:source-%u.bin",stage);
  auto* file=fopen(path,"wb");
  const unsigned header[]={0x53343441,stage,records,unsigned(bytes.size())};
  valid=file && fwrite(header,sizeof(header),1,file)==1;
  if(file){valid=valid && fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    valid=fclose(file)==0 && valid;}
  printf("A44SOURCE stage=%u valid=%u records=%u bytes=%u\n",stage,unsigned(valid),records,unsigned(bytes.size()));
}
}
