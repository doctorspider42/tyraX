#pragma once
#ifdef WORLD_BODY_HOST
#include "tyrax2-world-body.hpp"
#else
#include "debug/world_body.hpp"
#endif
#include <array>
namespace WorldPayload {
using Quad=WorldBody::Quad;
// Parse the actual flattened SDK stream. No search through vertex data for opcodes.
inline std::array<const Quad*,3> decode(const std::vector<Quad>& body,
    unsigned count,bool textured,unsigned parity,bool first,unsigned address){
  const unsigned streams=textured?3:2;
  if(count<3 || count>72 || parity>1 || address>65535 ||
     body.size()!=4+streams*(count+1))throw std::invalid_argument("A31 payload shape");
  if(!(body[0]==Quad{{0,0,0x01000101,0x6c02c000}}) ||
     body[1].words[3]!=(count|(63U<<10)) || body[2].words[3]!=(2U|parity))
    throw std::invalid_argument("A31 payload native header");
  std::array<const Quad*,3> result{};
  for(unsigned stream=0;stream<streams;++stream){
    const unsigned at=3+stream*(count+1);
    const Quad expected{{0,0,0x01000101,0x6c00c000|(count<<16)|(2+stream*count)}};
    if(!(body[at]==expected))throw std::invalid_argument("A31 payload unpack");
    // Actual SDK order is XYZ, ST, colour for TC; XYZ, colour for C.
    result[stream==0?0:(!textured || stream==2?1:2)]=body.data()+at+1;
  }
  const Quad kick{{0,0,0x11000000,first?(0x14000000|address):0x17000000}};
  if(!(body.back()==kick))throw std::invalid_argument("A31 payload kick");
  return result;
}
}
