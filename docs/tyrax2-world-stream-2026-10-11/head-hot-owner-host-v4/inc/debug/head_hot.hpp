#pragma once
#include "debug/object_head.hpp"
#include <memory>

namespace HeadHot {
inline bool active=false;
inline unsigned liveWords=0,peakWords=0;
constexpr unsigned WordBudget=32768;
inline unsigned trialDraws=0,trialWords=0,trialCompiles=0;
struct Cell {
  ObjectHead::Image image;
  ObjectHead::Facts facts;
  unsigned words=0;
  ~Cell(){liveWords-=words;}
};
struct Owner {
  inline static Owner* first=nullptr;
  std::unique_ptr<Owner>* attachment=nullptr;
  Owner* previous=nullptr;
  Owner* next=nullptr;
  std::array<std::unique_ptr<Cell>,4> cells;
  std::uint64_t generation=0;
  explicit Owner(std::unique_ptr<Owner>* slot=nullptr):attachment(slot){
    if(!slot)return;
    next=first;if(first)first->previous=this;first=this;
  }
  ~Owner(){
    if(!attachment)return;
    if(previous)previous->next=next;else first=next;
    if(next)next->previous=previous;
  }
  Owner(const Owner&)=delete;
  Owner& operator=(const Owner&)=delete;
  static void releaseAll(){while(first)first->attachment->reset();}
  bool install(unsigned index,ObjectHead::Facts& facts,const ObjectHead::Word* data,
               unsigned words,const ObjectHead::ClipReference& ref){
    if(index>=cells.size() || !words || words>32 || words>WordBudget-liveWords ||
       generation==UINT64_MAX)return false;
    facts.generation=++generation;
    auto next=std::make_unique<Cell>();
    if(!next->image.compile(facts,data,words,ref))return false;
    next->facts=facts;
    peakWords=std::max(peakWords,liveWords+words);
    liveWords+=words;next->words=words;
    cells[index]=std::move(next);return true;
  }
};
struct Attachment {
  std::unique_ptr<Owner> value;
  Attachment()=default;
  Attachment(const Attachment&){}
  Attachment& operator=(const Attachment& b){if(this!=&b)value.reset();return *this;}
  Attachment(Attachment&& b)noexcept:value(std::move(b.value)){
    if(value && value->attachment)value->attachment=&value;
  }
  Attachment& operator=(Attachment&& b)noexcept{
    if(this!=&b){value=std::move(b.value);if(value && value->attachment)value->attachment=&value;}
    return *this;
  }
};
struct Output {
  std::array<ObjectHead::Word,32> words{};
  unsigned count=0;
};
}
