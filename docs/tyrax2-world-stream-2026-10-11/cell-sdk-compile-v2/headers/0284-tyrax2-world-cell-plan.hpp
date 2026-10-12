#pragma once
#include "tyrax2-world-strip-plan.hpp"
#include <limits>

// Private source compiler for a whole cell. No camera-dependent package route,
// VIF emission or hardware texture reservation is implemented by this model.
namespace WorldCellPlan {
using Quad = WorldStripPlan::Quad;
using Input = WorldStripPlan::Input;
enum class Topology { Triangles, IndependentStrips };
struct MaterialEpoch {
  std::uint64_t generation;
  std::uint32_t slot;
};
struct PassSource {
  std::array<Input,3> streams;
  std::uint32_t count, run, capacity;
  bool textured;
  Topology topology;
  MaterialEpoch material;
};
struct Part {
  std::uint32_t parity = 0;
  std::array<std::vector<Quad>,3> streams;
};
struct Pass {
  Topology topology;
  bool textured;
  MaterialEpoch material;
  std::vector<Part> parts;
};
struct Cell {
  std::uint64_t generation;
  std::uint64_t bytes = 0;
  std::vector<Pass> passes;
};

inline std::shared_ptr<const Cell> compile(const std::vector<PassSource>& sources,
                                          std::uint64_t generation,
                                          std::uint64_t byteBudget) {
  if(!generation || sources.empty() || sources.size()>64 || !byteBudget)
    throw std::invalid_argument("A44 invalid cell generation/pass budget");
  auto result=std::make_shared<Cell>();result->generation=generation;
  for(const auto& s:sources){
    if(!s.count || s.capacity<3 || s.capacity>72 || !s.material.generation)
      throw std::invalid_argument("A44 incomplete pass/material epoch");
    const unsigned streams=s.textured?3:2;
    for(unsigned k=0;k<streams;++k)
      if(!s.streams[k].data || s.streams[k].count<s.count)
        throw std::invalid_argument("A44 absent/short cell stream");
    if(s.topology!=Topology::Triangles && s.topology!=Topology::IndependentStrips)
      throw std::invalid_argument("A44 unknown topology");
    if(s.topology==Topology::Triangles && (s.count%3 || s.run))
      throw std::invalid_argument("A44 malformed triangle list");
    if(s.topology==Topology::IndependentStrips &&
       (s.run<3 || s.run%3 || (s.count%s.run && s.count%s.run<3)))
      throw std::invalid_argument("A44 malformed independent strip");
    Pass pass{s.topology,s.textured,s.material,{}};
    auto append=[&](std::uint32_t at,std::uint32_t count,std::uint32_t parity){
      const auto bytes=std::uint64_t(count)*streams*sizeof(Quad);
      if(bytes>byteBudget-result->bytes)
        throw std::length_error("A44 cell exceeds owned-stream budget");
      Part p;p.parity=parity;
      for(unsigned k=0;k<streams;++k)
        p.streams[k].assign(s.streams[k].data+at,s.streams[k].data+at+count);
      pass.parts.push_back(std::move(p));result->bytes+=bytes;
    };
    if(s.topology==Topology::Triangles){
      const auto step=s.capacity/3*3;
      for(std::uint32_t first=0;first<s.count;){
        const auto count=std::min(step,s.count-first);
        append(first,count,0);first+=count;
      }
    }else{
      for(std::uint32_t base=0;base<s.count;){
        const auto count=std::min(s.run,s.count-base);
        for(std::uint32_t first=0;first<count-2;){
          const auto triangles=std::min(s.capacity-2,count-2-first);
          append(base+first,triangles+2,first&1);first+=triangles;
        }
        base+=count;
      }
    }
    result->passes.push_back(std::move(pass));
  }
  return result;
}

class Owner {
 public:
  void replace(const std::vector<PassSource>& sources,std::uint64_t budget){
    if(generation_==std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("A44 cell generation exhausted");
    auto next=compile(sources,generation_+1,budget);
    cell_=std::move(next);++generation_;
  }
  std::shared_ptr<const Cell> lease()const{return cell_;}
  bool admits(const std::shared_ptr<const Cell>& cell)const{
    return cell && cell==cell_ && cell->generation==generation_;
  }
  void retire(){cell_.reset();}
 private:
  std::uint64_t generation_=0;
  std::shared_ptr<const Cell> cell_;
};
}
