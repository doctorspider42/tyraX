#pragma once
#include "renderer/core/3d/bbox/core_bbox.hpp"
#include <algorithm>
#include <memory>

namespace WorldFront {
inline bool active=false;
inline unsigned live=0,peak=0;
constexpr unsigned OwnerLimit=1024;
inline unsigned movingInside=0,movingPartial=0,movingOutside=0;
inline unsigned coldInside=0,coldPartial=0,coldOutside=0;
inline unsigned headChecks=0,headWords=0,headHash=2166136261U;
inline void resetOracle(){coldInside=coldPartial=coldOutside=headChecks=headWords=0;headHash=2166136261U;}
struct Owner {
  inline static Owner* first=nullptr;
  std::unique_ptr<Owner>* attachment;
  Owner* previous=nullptr;
  Owner* next=nullptr;
  Tyra::CoreBBox bounds;
  Tyra::M4x4 model;
  const Tyra::Vec4* vertices=nullptr;
  const void* colors=nullptr;
  const Tyra::Vec4* coordinates=nullptr;
  unsigned count=0,run=0,capacity=0,bboxVersion=0,epoch=0,bufferSize=0;
  unsigned stamps[3]={};
  bool textured=false;
  explicit Owner(std::unique_ptr<Owner>* slot):attachment(slot){
    next=first;if(first)first->previous=this;first=this;
    ++live;peak=std::max(peak,live);
  }
  ~Owner(){
    if(previous)previous->next=next;else first=next;
    if(next)next->previous=previous;
    --live;
  }
  Owner(const Owner&)=delete;
  Owner& operator=(const Owner&)=delete;
  static void releaseAll(){while(first)first->attachment->reset();}
};
struct Attachment {
  std::unique_ptr<Owner> value;
  Attachment()=default;
  Attachment(const Attachment&){}
  Attachment& operator=(const Attachment& b){if(this!=&b)value.reset();return *this;}
  Attachment(Attachment&& b)noexcept:value(std::move(b.value)){if(value)value->attachment=&value;}
  Attachment& operator=(Attachment&& b)noexcept{
    if(this!=&b){value=std::move(b.value);if(value)value->attachment=&value;}return *this;
  }
};
static_assert(sizeof(Owner)<=1024,"World frontend owner exceeds its bounded footprint");
}
