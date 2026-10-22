#include "tyrax2-world-input-compact.hpp"
#include <cassert>
#include <cstdio>
using namespace WorldInputCompact;
struct Decoded {
  State state;
  std::array<std::array<Word,4>,1024> memory{};
  std::vector<std::array<std::array<Word,4>,1024>> kicks;
};
// Independent word-level interpreter for the explicitly admitted VIF subset.
static Decoded decode(const std::vector<Word>& words,State initial,unsigned tops) {
  Decoded d{initial,{}, {}};unsigned cycle=0;
  for(std::size_t at=0;at<words.size();) {
    const Word code=words[at++];const unsigned op=code>>24;
    if(!code)continue;
    if(op==1){cycle=code&65535;continue;}
    if(op==0x30){for(auto& v:d.state.row)v=words.at(at++);continue;}
    if(op==0x20){d.state.mask=words.at(at++);continue;}
    if(op==0x11)continue;
    if(op==0x14 || op==0x17){d.kicks.push_back(d.memory);continue;}
    assert((op==0x6c || op==0x78) && cycle==0x101);
    const unsigned count=(code>>16)&255;assert(count);
    const unsigned address=(code&1023)+((code&0x8000)?tops:0);
    const unsigned lanes=op==0x6c?4:3;
    for(unsigned i=0;i<count;++i)for(unsigned lane=0;lane<4;++lane) {
      Word value=lane<lanes?words.at(at+i*lanes+lane):0;
      if(op==0x78){
        const unsigned mode=(d.state.mask>>(8*(i%4)+2*lane))&3;
        assert(mode<2);if(mode==1)value=d.state.row[lane];
      }
      d.memory.at(address+i)[lane]=value;
    }
    at+=count*lanes;
  }
  return d;
}
static std::vector<Word> body(unsigned count,unsigned streams,unsigned variant) {
  std::vector<Word> out;
  for(unsigned part=0;part<3;++part) {
    out.insert(out.end(),{0,0,0x01000101,0x6c02c000,
                         0x45000000,0x45000000,0x47000000,count|(63<<10),
                         0x100,0x9000,0,part&1});
    for(unsigned stream=0;stream<streams;++stream) {
      out.insert(out.end(),{0,0,0x01000101,0x6c00c000|(count<<16)|(2+stream*count)});
      const Word w=variant==0?0x3f800000:variant==1?0x80000000:variant==2?0x7fc12345:0x43000000;
      for(unsigned i=0;i<count;++i)
        out.insert(out.end(),{0x7fc00000^i,0x80000000+i,stream+part*100,
                             variant==3 && i==count-1?w^1:w});
    }
    out.insert(out.end(),{0,0,0x11000000,part?0x17000000u:0x14000120u});
  }
  return out;
}
int main(int argc,char** argv) {
  unsigned cases=0,compressed=0,controls=0;
  for(unsigned count=3;count<=72;++count)for(unsigned streams=2;streams<=3;++streams)
  for(unsigned variant=0;variant<4;++variant)for(unsigned tops:{0u,256u}) {
    const State incoming{{0xdeadbeef,0x80000000,0x7fc12345,0x12345678},0xabcdef01};
    const auto source=body(count,streams,variant);const auto saved=source;
    const auto packed=compile(source,streams,incoming);
    const auto a=decode(source,incoming,tops),b=decode(packed.words,incoming,tops);
    assert(a.kicks==b.kicks && a.memory==b.memory);
    assert(a.state.row==b.state.row && a.state.mask==b.state.mask && source==saved);
    assert(packed.words.size()<=source.size());
    const auto zero=compile(source,streams,State{{0,0,0,0},0});
    assert(canonicalize(zero.words)==source);
    const std::vector<Word> material{0,0,0x11000000,0x50000002,
      0x30000000,0x78000000,0x20000000,0x17000000,1,2,3,4};
    auto encoded=material,original=material;
    encoded.insert(encoded.end(),zero.words.begin(),zero.words.end());
    original.insert(original.end(),source.begin(),source.end());
    assert(canonicalize(encoded)==original);
    if(variant==3)assert(!packed.compacted);
    compressed+=packed.compacted;++cases;
  }
  const auto source=body(72,3,0);const State state{{0,0,0,0},0};
  for(unsigned fault=0;fault<8;++fault) {
    auto bad=source;unsigned streams=3;
    if(fault==0)bad.clear();
    if(fault==1)bad.pop_back();
    if(fault==2)bad[3]^=1;
    if(fault==3)bad[7]=73;
    if(fault==4)bad[15]^=1;
    if(fault==5)bad.back()=0x15000000;
    if(fault==6)streams=4;
    if(fault==7)bad[2]=0x01000202;
    bool rejected=false;try{compile(bad,streams,state);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);++controls;
  }
  const auto compact=compile(source,3,state);
  for(unsigned fault=0;fault<5;++fault) {
    auto bad=compact.words;
    if(fault==0)bad[13]=1;
    if(fault==1)bad[18]^=1;
    if(fault==2)bad[23]=0x79000000;
    if(fault==3)bad[246]=1;
    if(fault==4)bad.resize(24);
    bool rejected=false;
    try{canonicalize(bad);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
  std::printf("PASS inverseRejectingControls=5 materialPayloadBoundaries=1\n");
  std::printf("PASS cases=%u compactedStreams=%u rejectingControls=%u sourceWords=%zu compactWords=%zu exactVuAtEveryKick=1 restoredRowMask=1\n",
              cases,compressed,controls,source.size(),compact.words.size());
  if(argc==2) {
    FILE* file=std::fopen(argv[1],"rb");assert(file);
    assert(std::fseek(file,0,SEEK_END)==0);
    const long bytes=std::ftell(file);assert(bytes>64 && bytes%4==0);
    assert(std::fseek(file,0,SEEK_SET)==0);
    std::vector<Word> dump(static_cast<std::size_t>(bytes)/4);
    assert(std::fread(dump.data(),sizeof(Word),dump.size(),file)==dump.size());
    assert(std::fclose(file)==0);
    assert(dump[0]==0x43534957 && dump[1]==1 && dump[2]==60 && dump[3]==0);
    const State incoming{{dump[5],dump[6],dump[7],dump[8]},dump[4]};
    std::size_t at=9,oldWords=0,newWords=0;unsigned sdkCompacted=0,sdkFallback=0;
    for(unsigned record=0;record<60;++record) {
      assert(dump.size()-at>=6);
      const unsigned mode=dump[at],topology=dump[at+1],count=dump[at+2],pass=dump[at+3];
      assert(mode==record/30 && topology==(record/15)%2 && pass==record%5);
      assert(count==((record/5)%3==0?3u:(record/5)%3==1?78u:153u));
      assert(dump[at+4]<=1);
      const unsigned streams=dump[at+4]?3:2,length=dump[at+5];at+=6;
      assert(length && length<=dump.size()-at && length%4==0);
      const std::vector<Word> sdk(dump.begin()+at,dump.begin()+at+length);at+=length;
      const auto packed=compile(sdk,streams,incoming);
      assert(canonicalize(compile(sdk,streams,State{{0,0,0,0},0}).words)==sdk);
      for(unsigned tops:{0u,256u}) {
        const auto original=decode(sdk,incoming,tops),decoded=decode(packed.words,incoming,tops);
        assert(original.kicks==decoded.kicks && original.memory==decoded.memory);
        assert(original.state.row==decoded.state.row && original.state.mask==decoded.state.mask);
      }
      oldWords+=sdk.size();newWords+=packed.words.size();
      sdkCompacted+=packed.compacted;sdkFallback+=packed.fallback;
    }
    assert(dump.size()-at==7 && dump[at]==0x454e4957 && dump[at+1]==0);
    assert(dump[at+2]==incoming.mask);
    for(unsigned i=0;i<4;++i)assert(dump[at+3+i]==incoming.row[i]);
    std::printf("PASS actualSdkRecords=60 oldWords=%zu compactWords=%zu compactedStreams=%u fallbackStreams=%u exactVuAtEveryKick=1 observedStateStable=1 gpuExecuted=0\n",
                oldWords,newWords,sdkCompacted,sdkFallback);
  } else assert(argc==1);
}
