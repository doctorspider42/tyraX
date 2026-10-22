#define main preparedFoundationMain
#include "verify-prepared-front.cpp"
#undef main
#include "debug/world_front.hpp"
#include "actual-ticket-admission.inc"
#include "verify-world-front-producer.inc"
#include "actual-ticket-producer.inc"

int main(){
  assert(preparedFoundationMain()==0);
  {
    std::vector<Tyra::Vec4> xyz(6,Tyra::Vec4(1,2,3,1)),rgba(6,Tyra::Vec4(128,128,128,128));
    Tyra::M4x4 model;for(unsigned i=0;i<16;++i)model.data[i]=(i%5==0)?1.0F:0.0F;
    unsigned stamps[3]={1,2,3};
    Tyra::TicketInfo info{&model,true,Tyra::TyraMVP,Tyra::PipelineInfoBagFrustumCulling_Precise};
    Tyra::TicketColor color{rgba.data(),nullptr,&stamps[1]};
    Tyra::TicketTexture texture{&model,xyz.data(),&stamps[2],false};
    Tyra::StaPipBag bag{true,xyz.data(),&stamps[0],6,6,&info,&color,nullptr,nullptr,&texture,9};
    Bounds boxes(xyz.data(),6,6);Tyra::StaPipCore producer;producer.cacher.boxes=&boxes;
    WorldFront::active=true;
    assert(producer.prepareWorldTicket(&bag));auto* original=bag.worldTicket.value.get();
    auto rejected=[&](){
      assert(!producer.prepareWorldTicket(&bag));assert(bag.worldTicket.value.get()==original);
      assert(WorldFront::live==1 && producer.prim.fogging==0 && producer.prim.extra==17 && producer.lod.value==21);
      assert(ClipConsumer::observe && ClipTrial::observe);
    };
    producer.cacher.fail=true;rejected();producer.cacher.fail=false;
    producer.cacher.missing=true;rejected();producer.cacher.missing=false;
    producer.qbufferRenderer.fail=true;rejected();producer.qbufferRenderer.fail=false;
    producer.qbufferRenderer.ready=false;rejected();producer.qbufferRenderer.ready=true;
    producer.qbufferRenderer.clipping=false;rejected();producer.qbufferRenderer.clipping=true;
    producer.qbufferRenderer.overrides=true;rejected();producer.qbufferRenderer.overrides=false;
    producer.renderer.blss.active=true;rejected();producer.renderer.blss.active=false;
    WorldFront::active=false;rejected();WorldFront::active=true;
    std::vector<WorldFront::Attachment> budget;
    for(unsigned i=1;i<WorldFront::OwnerLimit;++i){budget.emplace_back();auto& slot=budget.back();slot.value=std::make_unique<WorldFront::Owner>(&slot.value);}
    assert(!producer.prepareWorldTicket(&bag));assert(bag.worldTicket.value.get()==original);
    budget.back().value.reset();assert(producer.prepareWorldTicket(&bag));
    assert(bag.worldTicket.value.get()!=original && WorldFront::live==1023 && WorldFront::peak<=1024);
    WorldFront::Owner::releaseAll();WorldFront::active=false;
    puts("PASS extracted producer failure/replacement/state/old+new budget; SDK/compiler shims, no target proof");
  }
  unsigned controls=0;
  for(unsigned round=0;round<64;++round){
    assert(WorldFront::live==0 && !WorldFront::Owner::first);
    std::vector<Tyra::Vec4> xyz(6,Tyra::Vec4(1,2,3,1)),rgba(6,Tyra::Vec4(128,128,128,128));
    Tyra::M4x4 model;for(unsigned i=0;i<16;++i)model.data[i]=(i%5==0)?1.0F:0.0F;
    unsigned stamps[3]={1,2,3};
    Tyra::TicketInfo info{&model,true,Tyra::TyraMVP,Tyra::PipelineInfoBagFrustumCulling_Precise};
    Tyra::TicketColor color{rgba.data(),nullptr,&stamps[1]};
    Tyra::TicketTexture texture{&model,xyz.data(),&stamps[2],false};
    Tyra::StaPipBag bag{true,xyz.data(),&stamps[0],6,6,&info,&color,nullptr,nullptr,&texture,9};
    assert(Tyra::ticketShape(&bag));
    WorldFront::Attachment a;
    a.value=std::make_unique<WorldFront::Owner>(&a.value);
    auto& t=*a.value;
    t.vertices=xyz.data();t.colors=rgba.data();t.coordinates=xyz.data();
    t.count=6;t.run=6;t.capacity=6;t.bboxVersion=9;t.epoch=7;t.bufferSize=512;t.textured=true;
    for(unsigned i=0;i<3;++i)t.stamps[i]=stamps[i];memcpy(t.model.data,model.data,64);
    Bounds original(xyz.data(),6,6);t.bounds=*original.getMainBBox();
    for(unsigned i=0;i<8;++i)assert(same(t.bounds[i],(*original.getMainBBox())[i]));
    assert(Tyra::actualTicketMatches(t,&bag,7,512));
    auto reject=[&](Tyra::StaPipBag changed){
      assert(!Tyra::ticketShape(&changed) || !Tyra::actualTicketMatches(t,&changed,7,512));++controls;
    };
    auto b=bag;b.vertices=xyz.data()+1;reject(b);b=bag;b.count=3;reject(b);
    b=bag;b.packageSize=3;reject(b);b=bag;b.bboxVersion++;reject(b);
    b=bag;b.stripped=false;reject(b);b=bag;b.contentVersion=nullptr;reject(b);
    b=bag;b.lighting=&model;reject(b);b=bag;b.billboard=&model;reject(b);
    b=bag;b.color=nullptr;reject(b);b=bag;b.info=nullptr;reject(b);
    b=bag;b.texture=nullptr;reject(b);
    assert(!Tyra::actualTicketMatches(t,&bag,8,512));++controls;
    assert(!Tyra::actualTicketMatches(t,&bag,7,256));++controls;
    for(unsigned i=0;i<3;++i){++stamps[i];reject(bag);--stamps[i];}
    info.fullClipChecks=false;reject(bag);info.fullClipChecks=true;
    info.transformationType=Tyra::TyraMP;reject(bag);info.transformationType=Tyra::TyraMVP;
    info.frustumCulling=Tyra::PipelineInfoBagFrustumCulling_None;reject(bag);
    info.frustumCulling=Tyra::PipelineInfoBagFrustumCulling_Precise;
    texture.coordinatesAreNormals=true;reject(bag);texture.coordinatesAreNormals=false;
    color.single=&model;reject(bag);color.single=nullptr;
    color.many=rgba.data()+1;reject(bag);color.many=rgba.data();
    texture.coordinates=xyz.data()+1;reject(bag);texture.coordinates=xyz.data();
    for(unsigned i=0;i<16;++i){model.data[i]+=1;reject(bag);model.data[i]-=1;}
    std::vector<WorldFront::Attachment> owners;
    for(unsigned i=1;i<WorldFront::OwnerLimit;++i){
      owners.emplace_back();auto& slot=owners.back();
      slot.value=std::make_unique<WorldFront::Owner>(&slot.value);
    }
    assert(WorldFront::live==WorldFront::OwnerLimit && WorldFront::peak<=WorldFront::OwnerLimit);
    WorldFront::Attachment copied(a);assert(!copied.value && a.value);copied=a;assert(!copied.value);
    a=a;assert(a.value);WorldFront::Attachment moved(std::move(a));assert(moved.value && !a.value);
    moved=std::move(moved);assert(moved.value);
    owners.back().value.reset();
    copied.value=std::make_unique<WorldFront::Owner>(&copied.value);
    copied=std::move(moved);assert(copied.value && !moved.value);
    WorldFront::Owner::releaseAll();assert(!copied.value && !WorldFront::Owner::first && WorldFront::live==0);
  }
  printf("PASS actual ticket field admission controls=%u and64 owner/copy/move/registry rounds; pipeline/ABI shims, no target/FPS proof\n",controls);
}
