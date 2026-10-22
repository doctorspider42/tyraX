namespace Tyra {
void StaPipQBufferRenderer::testObjectHead(StaPipBag* bag,M4x4* mvp,
    packet2_t* packet,u32 start,bool spotActive,bool mayClip,bool single){
  if(!ClipConsumer::observe || bag->lighting || bag->billboard || vuCustomEnabled ||
      (bag->texture && bag->texture->coordinatesAreNormals))return;
  const unsigned end=packet2_get_qw_count(packet),n=end-start;
  if(!n || n>32)return;
  std::array<ObjectHead::Word,32> source{},output{};
  memcpy(source.data(),packet->base+start,n*16);
  int spot=-1,options=-1;
  for(unsigned at=0;at<n;){
    const auto& w=source[at].w;const unsigned id=(w[0]>>28)&7U;
    if(id==3){++at;continue;}
    const unsigned size=w[0]&0xffffU;
    if(id!=1 || !size || n-at<1+size)return;
    const unsigned addr=w[3]&1023U;
    if(addr==12)spot=at+1;if(addr==8)options=at+1;
    at+=1+size;
  }
  if(options<0 || unsigned(options)+3+(bag->texture?1:0)>n)return;
  ObjectHead::Facts f;f.epoch=worldEpoch;
  f.shape=(bag->texture?ObjectHead::Textured:0U)|(single?ObjectHead::Single:0U)|
      (spotActive?ObjectHead::Spot:0U)|(vu1Clipping && mayClip?ObjectHead::Clip:0U);
  if(spotActive && spot<0)return;
  const auto& opt=source[options];
  for(unsigned i=0;i<4;++i){
    f.fixed[i]=source[options+1].w[i];f.fixed[4+i]=source[options+2].w[i];
    f.fixed[8+i]=source[n-1].w[i];
  }
  ObjectHead::ClipReference ref;ref.epoch=worldEpoch;
  if(f.shape&ObjectHead::Clip){
    if(!clipBlockVifQw)return; // Initial inline clip capture remains ordinary.
    ref.tag.w={0x30000000U|clipBlockVifQw,reinterpret_cast<u32>(clipBlockVif)&0x0fffffffU,0,0};
    for(unsigned i=0;i<4;++i)f.fixed[12+i]=ref.tag.w[i];
  }
  static ObjectHead::Image images[16];static ObjectHead::Facts keys[16];
  static u64 generation=0;
  f.generation=keys[f.shape].generation;
  if(!(f==keys[f.shape])){
    TYRA_ASSERT(generation!=UINT64_MAX,"A33 head generation exhausted");
    f.generation=++generation;
    if(!images[f.shape].compile(f,source.data(),n,ref))return;
    keys[f.shape]=f;
  }
  std::array<ObjectHead::Word,4> matrix{};memcpy(matrix.data(),mvp->data,64);
  ObjectHead::Word color{};if(single)memcpy(&color,bag->color->single->rgba,16);
  ObjectHead::Live live;live.mvp=matrix.data();live.color=single?&color:nullptr;
  live.spot=spotActive?source.data()+spot:nullptr;
  live.fogScale=opt.w[2];live.fogOffset=opt.w[3];
  if(bag->texture)live.tex0={source[options+3].w[0],source[options+3].w[1]};
  TYRA_ASSERT(images[f.shape].write(f,live,output.data(),32),"A33 head write failed");
  TYRA_ASSERT(memcmp(source.data(),output.data(),n*16)==0,"A33 head differs from actual writer");
  ++HeadTrial::checks;HeadTrial::words+=n;
  for(unsigned i=0;i<n;++i)for(u32 v:output[i].w){HeadTrial::hash^=v;HeadTrial::hash*=16777619U;}
  if(WorldRuntime::active){memcpy(packet->base+start,output.data(),n*16);++HeadTrial::draws;}
}
}
