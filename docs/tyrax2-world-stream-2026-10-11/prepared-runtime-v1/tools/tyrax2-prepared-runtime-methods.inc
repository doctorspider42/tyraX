namespace Tyra {
static u32 frontStamp(const u32* p){return p?*p:0U;}
static bool frontEligible(const StaPipBag* b){
  return b && b->vertices && b->contentVersion && b->info && b->info->model && b->color &&
    ((b->color->single && !b->color->many) ||
     (!b->color->single && b->color->many && b->color->contentVersion)) &&
    !b->lighting && !b->billboard && b->info->fullClipChecks &&
    b->info->transformationType==TyraMVP &&
    b->info->frustumCulling==PipelineInfoBagFrustumCulling_Precise &&
    (b->info->shadingType==TyraShadingFlat || b->info->shadingType==TyraShadingGouraud) &&
    (b->info->textureMappingType==TyraLinear || b->info->textureMappingType==TyraNearest) &&
    (b->info->zTestType==PipelineZTest_Standard || b->info->zTestType==PipelineZTest_AllPass ||
     b->info->zTestType==PipelineZTest_TestOnly) &&
    (!b->texture || (b->texture->texture && b->texture->coordinates && b->texture->contentVersion &&
                    !b->texture->coordinatesAreNormals));
}
static PreparedFront::Key frontKey(const StaPipBag* b,u32 capacity,u32 epoch,
                                    StaPipVU1Program* program){
  PreparedFront::Key k;k.generation=b->frontGeneration;k.programEpoch=epoch;
  k.programAddress=program->getDestinationAddress();k.programClass=u32(program->getName());
  k.streams={reinterpret_cast<std::uintptr_t>(b->vertices),reinterpret_cast<std::uintptr_t>(b->color->many),
    reinterpret_cast<std::uintptr_t>(b->texture?b->texture->coordinates:nullptr),0};
  k.resources={reinterpret_cast<std::uintptr_t>(b->texture?b->texture->texture:nullptr),
    reinterpret_cast<std::uintptr_t>(b->color->single),reinterpret_cast<std::uintptr_t>(b->info)};
  k.content={frontStamp(b->contentVersion),frontStamp(b->color->contentVersion),
    frontStamp(b->texture?b->texture->contentVersion:nullptr),0};
  k.count=b->count;k.capacity=capacity;k.run=b->packageSize;k.bboxVersion=b->bboxVersion;
  k.shape=PreparedFront::Precise|PreparedFront::FullClip|PreparedFront::MVP|
    (b->stripped?PreparedFront::Stripped:0U)|(b->color->single?PreparedFront::SingleColor:0U)|
    (b->texture?PreparedFront::Textured:0U);
  const auto& i=*b->info;
  k.material[0]=u32(i.shadingType)|(u32(i.textureMappingType)<<3)|(u32(i.transformationType)<<6)|
    (u32(i.zTestType)<<8)|(u32(i.frustumCulling)<<11)|(u32(i.fullClipChecks)<<14)|
    (u32(i.blendingEnabled)<<15)|(u32(i.antiAliasingEnabled)<<16)|(u32(i.fogDisabled)<<17)|
    (u32(i.dynLightPick)<<18)|(u32(i.dynamicLightReceive)<<19)|(u32(i.spotLit)<<20)|
    (u32(i.dateLit)<<21)|(u32(i.blssProxy)<<22);
  k.material[1]=u32(i.additiveBlendFix)|(u32(i.subtractiveBlendFix)<<8);
  k.material[2]=u32(i.dynLightSkipSlot);k.material[3]=b->texture?b->texture->textureFunction:0;
  if(b->texture){const auto& w=*b->texture->texture->getWrapSettings();
    const u64 clamp=GS_SET_CLAMP(w.horizontal,w.vertical,w.minu,w.maxu,w.minv,w.maxv);
    k.material[4]=u32(clamp);k.material[5]=u32(clamp>>32);
  }
  k.material[6]=b->refineStrips;const float depth=RendererCoreDepth::scale;
  memcpy(&k.depthBits,&depth,4);return k;
}
bool StaPipCore::prepareFrontGeometry(StaPipBag* b){
  if((!FrontRuntime::active && !WorldRuntime::cold) || !frontEligible(b) ||
      qbufferRenderer.hasProgramOverrides() || rendererCore->blss.wantsProxies())return false;
  if(b->frontGeneration==UINT64_MAX)return false;
  ++b->frontGeneration;
  auto* program=qbufferRenderer.getCullProgramByBag(b);if(!program)return false;
  const auto key=frontKey(b,getMaxVertCountByBag(b),getWorldEpoch(),program);
  if(!PreparedFront::valid(key))return false;
  if(!b->frontOwner.value)b->frontOwner.value=std::make_unique<FrontRuntime::Owner>();
  return b->frontOwner.value->compile(key);
}
StaPipBagPackagesBBox* StaPipCore::admitFrontGeometry(StaPipBag* b){
  if((!FrontRuntime::active && !ClipConsumer::observe) || !b || !b->frontOwner.value ||
      !frontEligible(b) || qbufferRenderer.hasProgramOverrides() || rendererCore->blss.wantsProxies())return nullptr;
  auto* program=qbufferRenderer.getCullProgramByBag(b);if(!program)return nullptr;
  const auto key=frontKey(b,b->frontOwner.value->key().capacity,getWorldEpoch(),program);
  auto* bounds=b->frontOwner.value->admit(key);if(!bounds)return nullptr;
  if(ClipConsumer::observe){
    const u32 capacity=getMaxVertCountByBag(b);
    TYRA_ASSERT(capacity==key.capacity,"A32 stale capacity");
    StaPipBagPackagesBBox ordinary(b->vertices,b->count,capacity);
    TYRA_ASSERT(ordinary.getPartsCount()==bounds->getPartsCount(),"A32 stale parts");
    for(u32 j=0;j<8;++j)TYRA_ASSERT(memcmp((*ordinary.getMainBBox())[j].xyzw,
      (*bounds->getMainBBox())[j].xyzw,16)==0,"A32 stale main bounds");
    for(u32 p=0;p<ordinary.getPartsCount();++p)for(u32 j=0;j<8;++j)
      TYRA_ASSERT(memcmp(ordinary.getChildBBox1By3(p)[j].xyzw,
        bounds->getChildBBox1By3(p)[j].xyzw,16)==0,"A32 stale child bounds");
    ++FrontRuntime::sources;FrontRuntime::parts+=bounds->getPartsCount();
    FrontRuntime::word(key.count);FrontRuntime::word(key.capacity);FrontRuntime::word(key.run);
    FrontRuntime::word(key.shape);FrontRuntime::word(key.bboxVersion);
    FrontRuntime::word(key.programAddress);FrontRuntime::word(key.programClass);FrontRuntime::word(key.depthBits);
    for(u32 v:key.content)FrontRuntime::word(v);for(u32 v:key.material)FrontRuntime::word(v);
    for(u32 n=0;n<b->count;++n){
      for(const void* stream:{static_cast<const void*>(b->vertices),static_cast<const void*>(b->color->many),
        static_cast<const void*>(b->texture?b->texture->coordinates:nullptr)})if(stream){
        u32 bits[4];memcpy(bits,static_cast<const u8*>(stream)+16*n,16);for(u32 v:bits)FrontRuntime::word(v);
      }
    }
    if(FrontRuntime::active)++FrontRuntime::draws;
  }
  if(!FrontRuntime::active)return nullptr;
  if(ClipTrial::observe){++FrontRuntime::trialDraws;FrontRuntime::trialParts+=bounds->getPartsCount();}
  return bounds;
}
}
