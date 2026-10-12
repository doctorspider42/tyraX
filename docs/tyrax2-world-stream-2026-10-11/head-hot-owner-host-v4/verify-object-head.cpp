#include "tyrax2-object-head.hpp"
#include <packet2_utils.h>
#include <cassert>
#include <cstdio>
#include <cstring>

using ObjectHead::Word;
static unsigned state=193;
static unsigned number(){state=state*1664525U+1013904223U;return state;}
static Word randomWord(){Word w;for(auto& x:w.w)x=number();return w;}
static void block(packet2_t* p,unsigned address,const Word* words,unsigned n){
  packet2_utils_vu_open_unpack(p,address,false);
  for(unsigned i=0;i<n;++i){
    std::uint64_t a,b;std::memcpy(&a,&words[i].w[0],8);std::memcpy(&b,&words[i].w[2],8);
    packet2_add_2x_s64(p,a,b);
  }
  packet2_vif_pad128(p);
  packet2_chain_close_tag(p);
  packet2_vif_close_unpack_manual(p,n);
}
static unsigned original(unsigned shape,const ObjectHead::Live& live,
    const std::array<Word,4>& options,const Word& alpha,const ObjectHead::ClipReference& ref,
    Word* destination){
  packet2_t p{};p.base=reinterpret_cast<qword_t*>(destination);p.next=p.base;
  p.mode=P2_MODE_CHAIN;p.tte=1;p.max_qwords_count=32;
  block(&p,0,live.mvp,4);
  if(shape&ObjectHead::Spot)block(&p,12,live.spot,3);
  if(shape&ObjectHead::Clip){std::memcpy(p.next,&ref.tag,16);++p.next;}
  if(shape&ObjectHead::Single)block(&p,7,live.color,1);
  auto opt=options;opt[0].w[0]=unsigned(bool(shape&ObjectHead::Single));
  opt[0].w[1]=(shape&ObjectHead::Spot)?0xffffffffU:0U;
  opt[0].w[2]=live.fogScale;opt[0].w[3]=live.fogOffset;
  opt[1].w[2]=GS_REG_TEX1;opt[1].w[3]=0;
  opt[2].w[2]=GS_REG_TEST;opt[2].w[3]=0;
  opt[3].w={live.tex0[0],live.tex0[1],GS_REG_TEX0,0};
  block(&p,8,opt.data(),(shape&ObjectHead::Textured)?4:3);
  block(&p,21,&alpha,1);
  return unsigned(p.next-p.base);
}
int main(){
  static_assert(sizeof(Word)==16 && alignof(Word)==16);
  static_assert(sizeof(qword_t)==16 && sizeof(dma_tag_t)==16);
  unsigned cases=0,controls=0;
  for(unsigned shape=0;shape<16;++shape)for(unsigned trial=0;trial<64;++trial){
    ObjectHead::Facts f;f.generation=1;f.epoch=7;f.shape=shape;
    for(auto& x:f.fixed)x=number();
    ObjectHead::ClipReference ref;ref.epoch=7;ref.tag.w={0x3000000fU,0x10000,0,0};
    std::array<Word,4> mvp,opt;std::array<Word,3> spot;
    for(auto& w:mvp)w=randomWord();for(auto& w:opt)w=randomWord();
    for(auto& w:spot)w=randomWord();Word color=randomWord(),alpha=randomWord();
    alpha.w[2]=GS_REG_ALPHA;alpha.w[3]=0;
    ObjectHead::Live live;live.mvp=mvp.data();live.spot=spot.data();live.color=&color;
    live.fogScale=number();live.fogOffset=number();live.tex0={number(),number()};
    std::array<Word,32> source{},target{},expected{};
    const unsigned n=original(shape,live,opt,alpha,ref,source.data());
    ObjectHead::Image image;assert(image.compile(f,source.data(),n,ref));
    for(unsigned view=0;view<24;++view){
      for(auto& w:mvp)w=randomWord();for(auto& w:spot)w=randomWord();color=randomWord();
      live.fogScale=number();live.fogOffset=number();live.tex0={number(),number()};
      assert(original(shape,live,opt,alpha,ref,expected.data())==n);
      assert(image.write(f,live,target.data(),32));
      assert(std::memcmp(target.data(),expected.data(),n*16)==0);++cases;
    }
    alignas(16) std::array<unsigned char,32*16+16> raw{};
    assert(image.writeBytes(f,live,raw.data(),32));
    assert(std::memcmp(raw.data(),expected.data(),n*16)==0);
    const auto rawBefore=raw;
    assert(!image.writeBytes(f,live,raw.data()+1,32));
    assert(raw==rawBefore);
    auto rejectWrite=[&](const ObjectHead::Facts& bad){
      for(auto& w:target)w.w.fill(0xdeadbeefU);
      const auto before=target;assert(!image.write(bad,live,target.data(),32));
      assert(target==before);++controls;
    };
    auto bad=f;bad.generation++;rejectWrite(bad);bad=f;bad.epoch++;rejectWrite(bad);
    bad=f;bad.shape^=1;rejectWrite(bad);
    for(unsigned i=0;i<f.fixed.size();++i){bad=f;bad.fixed[i]^=1;rejectWrite(bad);}
    auto rejectLive=[&](const ObjectHead::Live& bad,unsigned capacity){
      for(auto& w:target)w.w.fill(0xdeadbeefU);
      auto before=target;assert(!image.write(f,bad,target.data(),capacity));
      assert(target==before);++controls;
    };
    rejectLive(live,n-1);
    auto noMvp=live;noMvp.mvp=nullptr;rejectLive(noMvp,32);
    auto alias=live;alias.mvp=target.data()+1;rejectLive(alias,32);
    if(shape&ObjectHead::Spot){auto badLive=live;badLive.spot=nullptr;rejectLive(badLive,32);
      badLive.spot=target.data();rejectLive(badLive,32);}
    if(shape&ObjectHead::Single){auto badLive=live;badLive.color=nullptr;rejectLive(badLive,32);
      badLive.color=target.data();rejectLive(badLive,32);}
    auto replacement=f;replacement.generation=2;
    // Walk actual SDK DMA tags, never infer command locations from random data.
    for(unsigned i=0;i<n;){
      const unsigned id=(source[i].w[0]>>28)&7;
      assert(id==1 || id==3);
      for(unsigned word=0;word<4;++word){
        auto corrupt=source;corrupt[i].w[word]^=0x80000000U;
        assert(!image.compile(replacement,corrupt.data(),n,ref));++controls;
        assert(image.write(f,live,target.data(),32));
      }
      i+=id==1?1+(source[i].w[0]&0xffffU):1;
    }
    assert(!image.compile(replacement,source.data(),n-1,ref));++controls;
    assert(image.write(f,live,target.data(),32));
    assert(!image.compile(f,source.data(),n,ref));++controls;
    assert(image.compile(replacement,source.data(),n,ref));assert(!image.write(f,live,target.data(),32));
  }
  std::printf("PASS actual SDK object-head writes=%u stale/invalid controls=%u; no runtime/FPS claim\n",cases,controls);
  return 0;
}
