namespace Tyra {
static ObjectHead::Facts hotHeadFacts(const lod_t* lod,const PipelineInfoBag* info,
    u32 epoch,u32 shape,u32 zMethod,const ObjectHead::ClipReference& ref){
  ObjectHead::Facts f;f.epoch=epoch;f.shape=shape;
  f.fixed[0]=lod->calculation;f.fixed[1]=lod->max_level;
  f.fixed[2]=lod->mag_filter;f.fixed[3]=lod->min_filter;
  f.fixed[4]=lod->mipmap_select;f.fixed[5]=lod->l;
  memcpy(&f.fixed[6],&lod->k,4);
  f.fixed[7]=u32(info->zTestType);f.fixed[8]=info->dateLit;f.fixed[9]=zMethod;
  f.fixed[10]=info->additiveBlendFix;f.fixed[11]=info->subtractiveBlendFix;
  if(shape&ObjectHead::Clip)for(unsigned n=0;n<4;++n)f.fixed[12+n]=ref.tag.w[n];
  return f;
}
bool StaPipQBufferRenderer::installObjectHead(StaPipBag* bag,ObjectHead::Facts& facts,
    unsigned index,const ObjectHead::ClipReference& ref){
  std::array<ObjectHead::Word,32> source{};unsigned n=0;
  auto block=[&](unsigned address,unsigned count){
    memcpy(&source[n++],&unpackHeader(address,count),16);
    const unsigned payload=n;n+=count;return payload;
  };
  block(VU1_MVP_MATRIX_ADDR,4);
  if(facts.shape&ObjectHead::Spot)block(VU1_LIGHTS_DIRS_ADDR,3);
  if(facts.shape&ObjectHead::Clip)source[n++]=ref.tag;
  if(facts.shape&ObjectHead::Single)block(VU1_SINGLE_COLOR_ADDR,1);
  const unsigned options=block(VU1_OPTIONS_ADDR,(facts.shape&ObjectHead::Textured)?4:3);
  source[options].w[0]=unsigned(bool(facts.shape&ObjectHead::Single));
  source[options].w[1]=(facts.shape&ObjectHead::Spot)?0xffffffffU:0U;
  auto pair=[&](unsigned at,u64 value,unsigned reg){
    source[at].w={u32(value),u32(value>>32),reg,0};
  };
  pair(options+1,GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,
    lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F)),GS_REG_TEX1);
  const int date=bag->info->dateLit?1:0;
  u64 test;
  if(bag->info->zTestType==PipelineZTest_AllPass)
    test=GS_SET_TEST(0,0,0,0,date,0,0,ZTEST_METHOD_ALLPASS);
  else if(bag->info->zTestType==PipelineZTest_TestOnly)
    test=GS_SET_TEST(DRAW_ENABLE,ATEST_METHOD_ALLFAIL,0x00,ATEST_KEEP_ZBUFFER,
      date,DRAW_DISABLE,DRAW_ENABLE,rendererCore->gs.zBuffer.method);
  else
    test=GS_SET_TEST(DRAW_ENABLE,ATEST_METHOD_NOTEQUAL,0x00,ATEST_KEEP_ALL,
      date,DRAW_DISABLE,DRAW_ENABLE,rendererCore->gs.zBuffer.method);
  pair(options+2,test,GS_REG_TEST);
  if(facts.shape&ObjectHead::Textured)pair(options+3,0,GS_REG_TEX0);
  const unsigned alpha=block(VU1_ALPHA_ADDR,1);
  const u8 fix=bag->info->additiveBlendFix,sub=bag->info->subtractiveBlendFix;
  pair(alpha,sub?GS_SET_ALPHA(2,0,2,1,sub):fix?GS_SET_ALPHA(0,2,2,1,fix):
    GS_SET_ALPHA(0,1,0,1,0),GS_REG_ALPHA);
  bool installed=false;
  try {
    if(!bag->headOwner.value)bag->headOwner.value=std::make_unique<HeadHot::Owner>(&bag->headOwner.value);
    installed=bag->headOwner.value->install(index,facts,source.data(),n,ref);
  } catch(const std::bad_alloc&){return false;}
  if(installed && ClipTrial::observe)++HeadHot::trialCompiles;
  return installed;
}
bool StaPipQBufferRenderer::prepareObjectHead(StaPipBag* bag,M4x4* mvp,
    RendererCoreTextureBuffers* texBuffers,bool mayClip,void* destination,u32 capacity,u32& written){
  if(!bag || !bag->contentVersion || !bag->info || !bag->info->model || !bag->color ||
      bag->lighting || bag->billboard || vuCustomEnabled || hasProgramOverrides() ||
      rendererCore->blss.wantsProxies() ||
      ((bag->color->single==nullptr)==(bag->color->many==nullptr)) ||
      (bag->texture && (!bag->texture->texture || !texBuffers || bag->texture->coordinatesAreNormals)) ||
      (bag->info->zTestType!=PipelineZTest_Standard && bag->info->zTestType!=PipelineZTest_AllPass &&
       bag->info->zTestType!=PipelineZTest_TestOnly))return false;
  const bool clip=vu1Clipping && mayClip;
  if(clip && !clipBlockVifQw)return false;
  const auto& light=bagLight?*bagLight:rendererCore->spot;
  auto meshSpot=buildSpotForBag(light,bag->info->model);
  if(meshSpot.enabled && bagLightBounds && !repository.hasAnyOverride() &&
      stapipSpotHasNoInfluence(meshSpot,*bagLightBounds))meshSpot.enabled=false;
  const bool single=bag->color->single!=nullptr,spot=meshSpot.enabled;
  const unsigned shape=(bag->texture?ObjectHead::Textured:0U)|(single?ObjectHead::Single:0U)|
      (spot?ObjectHead::Spot:0U)|(clip?ObjectHead::Clip:0U);
  ObjectHead::ClipReference ref;ref.epoch=worldEpoch;
  if(clip)ref.tag.w={0x30000000U|clipBlockVifQw,reinterpret_cast<u32>(clipBlockVif)&0x0fffffffU,0,0};
  auto facts=hotHeadFacts(lod,bag->info,worldEpoch,shape,rendererCore->gs.zBuffer.method,ref);
  std::array<ObjectHead::Word,4> matrix{};memcpy(matrix.data(),mvp->data,64);
  std::array<ObjectHead::Word,3> spotWords{};
  if(spot){
    alignas(16) const float s[12]={meshSpot.position.x,meshSpot.position.y,meshSpot.position.z,
      meshSpot.invRange2,meshSpot.direction.x,meshSpot.direction.y,meshSpot.direction.z,
      meshSpot.cosCut2,meshSpot.color[0],meshSpot.color[1],meshSpot.color[2],meshSpot.invSoft};
    memcpy(spotWords.data(),s,48);
  }
  ObjectHead::Word color{};if(single)memcpy(&color,bag->color->single->rgba,16);
  float fogScale=rendererCore->fog.scale,fogOffset=rendererCore->fog.offset;
  bool constant=fogOffForBag;
  if(!constant && fogBox!=nullptr && fogScale<0.0F && !spot && bag->texture){
    const Vec4& lo=(*fogBox)[0];const Vec4& hi=(*fogBox)[7];const float* m=mvp->data;
    const float cx=0.5F*(lo.x+hi.x),hx=0.5F*(hi.x-lo.x);
    const float cy=0.5F*(lo.y+hi.y),hy=0.5F*(hi.y-lo.y);
    const float cz=0.5F*(lo.z+hi.z),hz=0.5F*(hi.z-lo.z);
    const float maxW=m[3]*cx+m[7]*cy+m[11]*cz+m[15]+
      fabsf(m[3])*hx+fabsf(m[7])*hy+fabsf(m[11])*hz;
    constant=maxW*fogScale+fogOffset>=255.0F;
  }
  if(constant){fogScale=0.0F;fogOffset=255.0F;}
  else {u32 bits;memcpy(&bits,&fogScale,4);
    if((bits&0xffffU)==0U && bits!=0U){bits|=1U;memcpy(&fogScale,&bits,4);}}
  ObjectHead::Live live;live.mvp=matrix.data();live.spot=spot?spotWords.data():nullptr;
  live.color=single?&color:nullptr;memcpy(&live.fogScale,&fogScale,4);memcpy(&live.fogOffset,&fogOffset,4);
  if(texBuffers){
    rendererCore->texture.updateClutBuffer(texBuffers->clut);
    texBuffers->core->info.function=bag->texture->textureFunction;
    const auto* tb=texBuffers->core;const auto* cl=&rendererCore->texture.clut;
    const u64 tex0=GS_SET_TEX0(tb->address>>6,tb->width>>6,tb->psm,tb->info.width,
      tb->info.height,tb->info.components,tb->info.function,cl->address>>6,
      cl->psm,cl->storage_mode,cl->start,cl->load_method);
    live.tex0={u32(tex0),u32(tex0>>32)};
  }
  const unsigned index=(spot?1U:0U)|(clip?2U:0U);
  auto* owner=bag->headOwner.value.get();
  auto* cell=owner?owner->cells[index].get():nullptr;
  facts.generation=cell?cell->facts.generation:0;
  if(!cell || !cell->image.writeBytes(facts,live,destination,capacity)){
    if(!installObjectHead(bag,facts,index,ref))return false;
    cell=bag->headOwner.value->cells[index].get();
    if(!cell->image.writeBytes(facts,live,destination,capacity))return false;
  }
  written=cell->image.size();clipper.setSpot(meshSpot);return true;
}
void StaPipQBufferRenderer::testPreparedObjectHead(StaPipBag* bag,M4x4* mvp,
    RendererCoreTextureBuffers* texBuffers,packet2_t* packet,u32 start,bool mayClip){
  HeadHot::Output prepared;
  if(!prepareObjectHead(bag,mvp,texBuffers,mayClip,prepared.words.data(),32,prepared.count))return;
  const unsigned n=packet2_get_qw_count(packet)-start;
  TYRA_ASSERT(n==prepared.count && memcmp(packet->base+start,prepared.words.data(),n*16)==0,
    "A34 semantic head differs from original writer");
  ++HeadTrial::checks;HeadTrial::words+=n;
  for(unsigned i=0;i<n;++i)for(u32 v:prepared.words[i].w){HeadTrial::hash^=v;HeadTrial::hash*=16777619U;}
  if(HeadHot::active){memcpy(packet->base+start,prepared.words.data(),n*16);++HeadTrial::draws;}
  else bag->headOwner.value.reset();
}
}
