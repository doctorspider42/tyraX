namespace Tyra {
static bool ticketShape(const StaPipBag* b){
  return b && b->stripped && b->vertices && b->contentVersion && b->count>=3 &&
    b->packageSize>=3 && b->packageSize%3==0 &&
    (b->count%b->packageSize==0 || b->count%b->packageSize>=3) &&
    b->info && b->info->model && b->info->fullClipChecks &&
    b->info->transformationType==TyraMVP &&
    b->info->frustumCulling==PipelineInfoBagFrustumCulling_Precise &&
    b->color && b->color->many && !b->color->single && b->color->contentVersion &&
    !b->lighting && !b->billboard &&
    (!b->texture || (b->texture->texture && b->texture->coordinates &&
      b->texture->contentVersion && !b->texture->coordinatesAreNormals));
}
bool StaPipCore::prepareWorldTicket(StaPipBag* b){
  if((!WorldFront::active && !WorldRuntime::cold) || !ticketShape(b) ||
      !qbufferRenderer.isVU1ClippingEnabled() || qbufferRenderer.hasProgramOverrides() ||
      rendererCore->blss.wantsProxies() || WorldFront::live>=WorldFront::OwnerLimit)return false;
  const u32 capacity=getMaxVertCountByBag(b);
  try {
    auto* boxes=cacher.getBBoxes(b->vertices,b->count,reinterpret_cast<u32>(b->vertices),b->bboxVersion,capacity);
    if(!boxes)return false;
    auto next=std::make_unique<WorldFront::Owner>(&b->worldTicket.value);
    next->bounds=*boxes->getMainBBox();next->model=*b->info->model;
    next->vertices=b->vertices;next->colors=b->color->many;
    next->coordinates=b->texture?b->texture->coordinates:nullptr;
    next->count=b->count;next->run=b->packageSize;next->capacity=capacity;
    next->bboxVersion=b->bboxVersion;next->epoch=getWorldEpoch();
    next->bufferSize=qbufferRenderer.getBufferSize();next->textured=b->texture!=nullptr;
    next->stamps[0]=*b->contentVersion;next->stamps[1]=*b->color->contentVersion;
    next->stamps[2]=b->texture?*b->texture->contentVersion:0;
    bool body=false;
    {
      struct Restore {
        prim_t& targetPrim;lod_t& targetLod;prim_t savedPrim;lod_t savedLod;
        bool consumer,trial;
        ~Restore(){targetPrim=savedPrim;targetLod=savedLod;
          ClipConsumer::observe=consumer;ClipTrial::observe=trial;}
      } restore{prim,lod,prim,lod,ClipConsumer::observe,ClipTrial::observe};
      ClipConsumer::observe=false;ClipTrial::observe=false;
      prim.fogging=rendererCore->fog.enabled && !b->info->fogDisabled?DRAW_ENABLE:DRAW_DISABLE;
      qbufferRenderer.setInfo(b->info);
      body=qbufferRenderer.prepareWorldCullBody(b,capacity);
    }
    if(!body)return false;
    b->worldTicket.value=std::move(next);return true;
  } catch(const std::bad_alloc&){return false;}
}
bool StaPipCore::renderWorldTicket(StaPipBag* b,StaPipLightReception reception){
  if(!WorldFront::active || !ticketShape(b) || !b->worldTicket.value ||
      !qbufferRenderer.isVU1ClippingEnabled() || qbufferRenderer.hasProgramOverrides() ||
      rendererCore->blss.wantsProxies() || telemetryEnabled || TargetAudit::active())return false;
  auto& t=*b->worldTicket.value;
  if(t.vertices!=b->vertices || t.colors!=b->color->many || t.count!=b->count ||
      t.run!=b->packageSize || t.bboxVersion!=b->bboxVersion || t.epoch!=getWorldEpoch() ||
      t.bufferSize!=qbufferRenderer.getBufferSize() || t.textured!=(b->texture!=nullptr) ||
      t.coordinates!=(b->texture?b->texture->coordinates:nullptr) ||
      t.stamps[0]!=*b->contentVersion || t.stamps[1]!=*b->color->contentVersion ||
      t.stamps[2]!=(b->texture?*b->texture->contentVersion:0) ||
      memcmp(t.model.data,b->info->model->data,sizeof(t.model.data)))return false;
  const auto& view=rendererCore->renderer3D.getViewProj();
  const bool reuse=transformCacheValid && transformCacheModelPtr==b->info->model &&
    memcmp(transformCacheModel.data,t.model.data,sizeof(t.model.data))==0 &&
    memcmp(transformCacheViewProj.data,view.data,sizeof(view.data))==0;
  Plane planes[6];
  if(reuse && transformCachePlanesValid){
    for(u8 i=0;i<6;++i)planes[i]=transformCacheObjectSpacePlanes[i];
  }else CoreBBox::computeObjectSpacePlanes(planes,
    rendererCore->renderer3D.frustumPlanes.getAll(),*b->info->model);
  auto route=t.bounds.frustumCheckAABB(planes);
  if(route==OUTSIDE_FRUSTUM){
    if(ClipConsumer::observe)++WorldFront::coldOutside;
    recordOutsideBag(b);if(ClipTrial::observe)++WorldFront::movingOutside;return true;
  }
  M4x4 mvp=reuse?transformCacheMvp:view* *b->info->model;
  if(!reuse){
    transformCacheValid=true;transformCacheModelPtr=b->info->model;
    transformCacheModel=*b->info->model;transformCacheViewProj=view;transformCacheMvp=mvp;
  }
  for(u8 i=0;i<6;++i)transformCacheObjectSpacePlanes[i]=planes[i];
  transformCachePlanesValid=true;
  if(route==PARTIALLY_IN_FRUSTUM){
    computeClipObjectSpacePlanes(mvp);
    if(CoreBBox::activePlaneMaskAABB(clipObjectSpacePlanes,t.bounds[0],t.bounds[7],8)==0)
      route=IN_FRUSTUM;
  }
  prim.fogging=rendererCore->fog.enabled && !b->info->fogDisabled?DRAW_ENABLE:DRAW_DISABLE;
  qbufferRenderer.ensureProgramSet(false);qbufferRenderer.setInfo(b->info);
  const bool inside=route==IN_FRUSTUM;
  if(inside){
    if(!qbufferRenderer.beginBakedBag(b,t.capacity,false) || !qbufferRenderer.worldCullReady()){
      qbufferRenderer.endBakedBag();return false;
    }
  } else if(!qbufferRenderer.worldBodyReady(b))return false;
  // All fallbacks occur before texture acquisition or any live head emission.
  const bool hasTexture=b->texture!=nullptr;
  const bool cheap=qbufferRenderer.isSubmissionBatchOpen() && inside && b->count<=t.capacity*15;
  bool resident=true,repeat=true;
  if(cheap && hasTexture){
    resident=rendererCore->texture.isResident(b->texture->texture);
    const auto* w=b->texture->texture->getWrapSettings();
    repeat=w->horizontal==WRAP_REPEAT && w->vertical==WRAP_REPEAT;
  }
  qbufferRenderer.setSubmissionBatchCandidate(cheap && resident && repeat,hasTexture);
  RendererCoreTextureBuffers storage;RendererCoreTextureBuffers* buffers=nullptr;
  if(hasTexture){const auto tex=rendererCore->texture.useTexture(b->texture->texture);
    storage={tex.id,tex.core,tex.clut};buffers=&storage;}
  const auto& wrap=hasTexture && b->texture->texture->getWrapSettings()->horizontal!=WRAP_REPEAT?
    *b->texture->texture->getWrapSettings():RendererCoreGS::repeatWrap();
  qbufferRenderer.setBagWrap(&wrap);qbufferRenderer.clearLastProgramName();
  const bool receive=b->info->dynamicLightReceive && reception!=StaPipLightReception::Suppress;
  const RendererCoreSpotLight* light=nullptr;
  if(receive && b->info->dynLightPick){
    const auto& lo=t.bounds[0];const auto& hi=t.bounds[7];
    const Vec4 mid((lo.x+hi.x)*0.5F,(lo.y+hi.y)*0.5F,(lo.z+hi.z)*0.5F,1.0F);
    const auto& m=*b->info->model;const Vec4 center=m*mid;
    const float scale=Math::sqrtNonNegative(m.data[0]*m.data[0]+m.data[1]*m.data[1]+m.data[2]*m.data[2]);
    const float ex=hi.x-lo.x,ey=hi.y-lo.y,ez=hi.z-lo.z;
    const float radius=0.5F*Math::sqrtNonNegative(ex*ex+ey*ey+ez*ez)*scale;
    light=rendererCore->pickDynLight(center,radius,b->info->dynLightSkipSlot);
  }
  if(!receive || !b->info->spotLit)light=&kNoSpotLight;
  qbufferRenderer.setBagLight(light,&t.bounds);
  qbufferRenderer.setFogFacts(prim.fogging!=DRAW_ENABLE,&t.bounds);
  qbufferRenderer.setBagMayClip(!inside);
  qbufferRenderer.sendObjectData(b,&mvp,buffers);
  qbufferRenderer.setClipperMVP(&mvp);
  if(inside){
    TYRA_ASSERT(qbufferRenderer.replayWholeBakedBag(),"A35 lost prepared cull body after head");
    qbufferRenderer.endBakedBag();if(ClipTrial::observe)++WorldFront::movingInside;
    if(ClipConsumer::observe)++WorldFront::coldInside;
  }else{
    TYRA_ASSERT(qbufferRenderer.replayPreparedWorld(b),"A35 lost prepared partial body after head");
    if(ClipTrial::observe)++WorldFront::movingPartial;
    if(ClipConsumer::observe)++WorldFront::coldPartial;
  }
  return true;
}
}
