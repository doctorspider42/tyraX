#define main object_head_sdk_tests
#include "verify-object-head.cpp"
#undef main
#include "debug/head_hot.hpp"
#include <memory>

int main(){
  unsigned rounds=0;
  for(unsigned shape=0;shape<16;++shape)for(unsigned round=0;round<8;++round){
    assert(HeadHot::liveWords==0);
    ObjectHead::Facts f;f.epoch=7;f.shape=shape;
    ObjectHead::ClipReference ref;ref.epoch=7;ref.tag.w={0x3000000fU,0x10000,0,0};
    std::array<Word,4> matrix{},options{};std::array<Word,3> spot{};
    Word color{},alpha{};alpha.w[2]=GS_REG_ALPHA;
    ObjectHead::Live live;live.mvp=matrix.data();live.spot=spot.data();live.color=&color;
    std::array<Word,32> source{},target{};
    unsigned n=original(shape,live,options,alpha,ref,source.data());
    {
      HeadHot::Attachment attached;attached.value=std::make_unique<HeadHot::Owner>();
      auto& owner=*attached.value;
      assert(owner.install(0,f,source.data(),n,ref));
      assert(HeadHot::liveWords==n);
      auto previous=f;
      assert(owner.install(0,f,source.data(),n,ref));
      assert(f.generation>previous.generation && HeadHot::liveWords==n);
      assert(!owner.cells[0]->image.write(previous,live,target.data(),32));
      auto bad=source;bad[0].w[0]^=0x80000000U;
      auto failed=f;
      assert(!owner.install(0,failed,bad.data(),n,ref));
      assert(owner.cells[0]->image.write(f,live,target.data(),32));
      assert(HeadHot::liveWords==n);
      assert(!owner.install(4,failed,source.data(),n,ref));
      owner.generation=UINT64_MAX;
      assert(!owner.install(0,failed,source.data(),n,ref));
      owner.generation=f.generation;
      // Consume the real shared budget with independent, valid owner cells.
      std::vector<std::unique_ptr<HeadHot::Owner>> owners;
      while(HeadHot::liveWords+n<=HeadHot::WordBudget){
        auto next=std::make_unique<HeadHot::Owner>();auto key=f;
        assert(next->install(0,key,source.data(),n,ref));owners.push_back(std::move(next));
      }
      auto before=HeadHot::liveWords;
      assert(!owner.install(0,failed,source.data(),n,ref));
      assert(before==HeadHot::liveWords && owner.cells[0]->image.write(f,live,target.data(),32));
      owners.clear();assert(HeadHot::liveWords==n);
      HeadHot::Attachment copy(attached);assert(!copy.value && attached.value);
      copy=attached;assert(!copy.value && attached.value);
      attached=attached;assert(attached.value && HeadHot::liveWords==n);
      HeadHot::Attachment moved(std::move(attached));assert(moved.value && !attached.value);
      copy=std::move(moved);assert(copy.value && !moved.value && HeadHot::liveWords==n);
      assert(copy.value->cells[0]->image.write(f,live,target.data(),32));
      assert(memcmp(target.data(),source.data(),n*16)==0);
    }
    assert(HeadHot::liveWords==0);++rounds;
  }
  assert(HeadHot::peakWords<=HeadHot::WordBudget);
  printf("PASS hot owner: %u budget/replacement/failure/generation/copy/move rounds; no target/FPS proof\n",rounds);
}
