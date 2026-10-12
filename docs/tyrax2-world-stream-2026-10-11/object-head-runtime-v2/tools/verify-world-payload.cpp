#define WORLD_BODY_HOST
#include "tyrax2-world-payload.hpp"
#include <cassert>
#include <iostream>
int main(){
  unsigned cases=0,controls=0;
  for(unsigned n=3;n<=72;++n)for(bool tex:{false,true})for(unsigned parity:{0U,1U})for(bool first:{false,true}){
    const unsigned streams=tex?3:2;
    std::vector<WorldBody::Quad> body(4+streams*(n+1));
    body[0]={{0,0,0x01000101,0x6c02c000}};body[1].words[3]=n|(63U<<10);body[2].words[3]=2|parity;
    for(unsigned s=0;s<streams;++s){
      const unsigned at=3+s*(n+1);body[at]={{0,0,0x01000101,0x6c00c000|(n<<16)|(2+s*n)}};
      for(unsigned i=0;i<n;++i)body[at+i+1]={{s+1,i,i^7,i+3}};
    }
    body.back()={{0,0,0x11000000,first?0x14000120U:0x17000000U}};
    const auto decoded=WorldPayload::decode(body,n,tex,parity,first,0x120);
    assert(decoded[0][0].words[0]==1 && decoded[1][0].words[0]==streams);
    assert(!tex?decoded[2]==nullptr:decoded[2][0].words[0]==2);++cases;
    for(unsigned kind=0;kind<9;++kind){
      auto bad=body;
      if(kind==0)bad.pop_back();
      if(kind==1)bad[0].words[3]^=0x4000;
      if(kind==2)bad[1].words[3]^=1;
      if(kind==3)bad[2].words[3]^=1;
      if(kind==4)bad[3].words[3]^=1;
      if(kind==5)bad[3].words[2]^=1;
      if(kind==6)bad.back().words[2]^=1;
      if(kind==7)bad.back().words[3]^=1;
      if(kind==8)bad[3].words[0]=1;
      try{(void)WorldPayload::decode(bad,n,tex,parity,first,0x120);assert(false);}
      catch(const std::invalid_argument&){++controls;}
    }
  }
  assert(cases==560 && controls==5040);
  std::cout<<"PASS A31 actual-payload decoder cases="<<cases<<" controls="<<controls<<"\n";
}
