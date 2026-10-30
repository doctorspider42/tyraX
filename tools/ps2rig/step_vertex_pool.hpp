// Private transient pose storage. No math, truncation, or collision filtering.
#pragma once
#include <cstddef>
#include <limits>
#include <vector>
namespace Tyra {
template<class Vertex>class StepVertexPool {
  std::vector<Vertex> previous_,predicted_;
  std::vector<std::size_t> offsets_;
 public:
  // Caller fences all consumers before begin; returned spans live until next begin.
  bool begin(const std::vector<int>& counts,int maximum){
    std::size_t total=0;
    for(int n:counts){
      if(n<0||n>maximum||std::size_t(n)>std::numeric_limits<std::size_t>::max()-total)return false;
      total+=std::size_t(n);
    }
    if(total>previous_.max_size()||total>predicted_.max_size())return false;
    offsets_.resize(counts.size()+1);total=0;
    for(std::size_t i=0;i<counts.size();++i){offsets_[i]=total;total+=std::size_t(counts[i]);}
    offsets_.back()=total;previous_.resize(total);predicted_.resize(total);return true;
  }
  Vertex* previous(std::size_t i){return offsets_[i]==offsets_[i+1]?nullptr:previous_.data()+offsets_[i];}
  Vertex* predicted(std::size_t i){return offsets_[i]==offsets_[i+1]?nullptr:predicted_.data()+offsets_[i];}
  std::size_t vertices()const{return previous_.size();}
};
}
