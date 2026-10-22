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
  std::array<std::unique_ptr<Cell>,4> cells;
  std::uint64_t generation=0;
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
  Attachment(Attachment&&)=default;
  Attachment& operator=(Attachment&&)=default;
};
struct Output {
  std::array<ObjectHead::Word,32> words{};
  unsigned count=0;
};
}
