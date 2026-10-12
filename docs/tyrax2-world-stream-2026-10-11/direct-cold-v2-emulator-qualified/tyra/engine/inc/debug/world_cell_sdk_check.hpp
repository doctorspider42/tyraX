#pragma once
#include "debug/world_cell_sdk.hpp"
#include <cstdio>
#include <cstring>

namespace WorldCellSdkCheck {
inline unsigned checks=0, parts=0, controls=0;
inline bool done=false;
inline void run(WorldCellSdk::Binding plain,WorldCellSdk::Binding textured) {
  using namespace WorldCellPlan;
  if(done)return;
  std::array<std::vector<Quad>,3> raw;
  const void* original[]={textured.bag->vertices,textured.bag->color->many,
                         textured.bag->texture->coordinates};
  for(unsigned s=0;s<3;++s){
    raw[s].resize(153);
    for(unsigned v=0;v<153;++v)
      std::memcpy(&raw[s][v],static_cast<const unsigned char*>(original[s])+
                  16*(v%textured.bag->count),16);
  }
  for(unsigned mode=0;mode<2;++mode)for(unsigned topology=0;topology<2;++topology)
  for(unsigned count:{3U,78U,153U}){
    std::vector<PassSource> sources;
    std::vector<WorldCellSdk::Binding> bindings;
    for(unsigned p=0;p<5;++p){
      const bool useTexture=(p==1)?!bool(mode):bool(mode);
      const auto shape=topology?Topology::IndependentStrips:Topology::Triangles;
      auto binding=useTexture?textured:plain;
      binding.material={17,p};
      PassSource source{{Input{raw[0].data(),count},Input{raw[1].data(),count},
                         Input{raw[2].data(),count}},count,topology?75U:0U,
                         WorldCellSdk::capacity(binding,shape),useTexture,shape,
                         binding.material};
      sources.push_back(source);bindings.push_back(binding);
    }
    auto cell=compile(sources,19,1024*1024);
    auto compiled=WorldCellSdk::compile(*cell,bindings,1024*1024);
    if(compiled.passes.size()!=cell->passes.size())
      throw std::runtime_error("A44 SDK pass loss");
    for(unsigned p=0;p<cell->passes.size();++p){
      const auto& source=cell->passes[p];const auto& output=compiled.passes[p];
      const auto& body=output.commands;
      std::size_t at=0;
      for(const auto& part:source.parts){
        const unsigned n=part.streams[0].size(),streams=source.textured?3:2;
        const unsigned length=4+streams*(n+1);
        if(length>body.size()-at ||
           !(body[at]==Quad{{0,0,0x01000101,0x6c02c000}}) ||
           body[at+1].words[3]!=(n|(63U<<10)) ||
           ((body[at+2].words[1]>>15)&7U)!=PRIM_TRIANGLE ||
           (topology && body[at+2].words[3]!=(2U|part.parity)))
          throw std::runtime_error("A44 SDK header shape");
        for(unsigned s=0;s<streams;++s){
          const unsigned pos=at+3+s*(n+1);
          const Quad unpack{{0,0,0x01000101,0x6c00c000|(n<<16)|(2+s*n)}};
          const unsigned logical=s==0?0:(!source.textured || s==2?1:2);
          if(!(body[pos]==unpack) || std::memcmp(body.data()+pos+1,
                    part.streams[logical].data(),n*16))
            throw std::runtime_error("A44 SDK stream delivery differs");
        }
        const Quad kick{{0,0,0x11000000,at?0x17000000:
                                       (0x14000000|output.programAddress)}};
        if(!(body[at+length-1]==kick))throw std::runtime_error("A44 SDK kick differs");
        at+=length;++parts;
      }
      if(at!=body.size() || output.material.slot!=p ||
         output.material.generation!=17 || output.programEpoch!=bindings[p].programEpoch)
        throw std::runtime_error("A44 SDK pass ownership differs");
    }
    auto reject=[&](const Cell& candidate,const std::vector<WorldCellSdk::Binding>& b,
                    std::uint64_t budget){
      bool rejected=false;
      try{WorldCellSdk::compile(candidate,b,budget);}
      catch(const std::exception&){rejected=true;}
      if(!rejected)throw std::runtime_error("A44 SDK invalid binding accepted");
      ++controls;
    };
    auto changed=bindings;changed[0].material.slot+=10;reject(*cell,changed,1024*1024);
    changed=bindings;changed[0].programEpoch=0;reject(*cell,changed,1024*1024);
    reject(*cell,bindings,1);
    auto broken=*cell;broken.passes[0].parts[0].streams[1].pop_back();
    reject(broken,bindings,1024*1024);
    if(topology){changed=bindings;changed[0].nativeStripImage=false;
      reject(*cell,changed,1024*1024);}
    ++checks;
  }
  done=true;
  std::printf("A44SDK checks=%u passes=%u parts=%u controls=%u sourceDelivery=PASS execution=EE compileOnly=0 gpuExecuted=0\n",
              checks,checks*5,parts,controls);std::fflush(stdout);
}
}
