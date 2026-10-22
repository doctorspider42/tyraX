namespace Tyra {
bool StaPipCore::prepareRoadPackages(StaPipBag* b) {
  if(!ticketShape(b) || !qbufferRenderer.isVU1ClippingEnabled() ||
     qbufferRenderer.hasProgramOverrides() || !ClipRing::active)return false;
  b->roadPrepared.reset();qbufferRenderer.collectWorldOwners();
  try {
    const u32 capacity=getMaxVertCountByBag(b);
    StaPipBagPackagesBBox boxes(b->vertices,b->count,capacity);
    auto owner=std::make_shared<RoadPrepared::Owner>();
    owner->bounds=*boxes.getMainBBox();owner->model=*b->info->model;
    owner->plan=RoadPackagePlan::compile<Vec4>(boxes,b->count,capacity,b->packageSize);
    if(!qbufferRenderer.compileRoadPackages(b,*owner,capacity)){++RoadPrepared::failed;return false;}
    b->roadPrepared=std::move(owner);++RoadPrepared::compiled;
    return true;
  }catch(const std::bad_alloc&){++RoadPrepared::failed;return false;}
}
bool StaPipCore::renderRoadPackages(StaPipBag* b,StaPipLightReception reception) {
  if((!RoadPrepared::active && !RoadPrepared::audit) || !b || !b->roadPrepared)return false;
  if(!ticketShape(b) || !qbufferRenderer.isVU1ClippingEnabled() ||
     qbufferRenderer.hasProgramOverrides() || !ClipRing::active ||
     rendererCore->blss.wantsProxies() || telemetryEnabled || TargetAudit::active())return false;
  auto& t=*b->roadPrepared;
  prim.fogging=rendererCore->fog.enabled && !b->info->fogDisabled?DRAW_ENABLE:DRAW_DISABLE;
  qbufferRenderer.setInfo(b->info);
  if(!qbufferRenderer.roadPackagesReady(b,t) ||
     memcmp(t.model.data,b->info->model->data,sizeof(t.model.data))){++RoadPrepared::unready;return false;}
  const auto& view=rendererCore->renderer3D.getViewProj();
  const bool reuse=transformCacheValid && transformCacheModelPtr==b->info->model &&
    memcmp(transformCacheModel.data,t.model.data,sizeof(t.model.data))==0 &&
    memcmp(transformCacheViewProj.data,view.data,sizeof(view.data))==0;
  Plane planes[6];
  if(reuse && transformCachePlanesValid)for(u8 i=0;i<6;++i)planes[i]=transformCacheObjectSpacePlanes[i];
  else CoreBBox::computeObjectSpacePlanes(planes,rendererCore->renderer3D.frustumPlanes.getAll(),t.model);
  auto route=t.bounds.frustumCheckAABB(planes);
  if(route==OUTSIDE_FRUSTUM){
    if(RoadPrepared::active){++RoadPrepared::outside;recordOutsideBag(b);return true;}
    return false;
  }
  M4x4 mvp=reuse?transformCacheMvp:view*t.model;
  if(route==PARTIALLY_IN_FRUSTUM){
    computeClipObjectSpacePlanes(mvp);
#if TYRA_STAPIP_GUARD_BAND_BAGS
    if(CoreBBox::activePlaneMaskAABB(clipObjectSpacePlanes,t.bounds[0],t.bounds[7],8)==0)route=IN_FRUSTUM;
#endif
  }
  // Whole-inside bags already have the O(1) ordinary replay. Do not replace
  // that with many per-package headers/REFs while qualifying the partial path.
  if(route==IN_FRUSTUM){if(RoadPrepared::active)++RoadPrepared::inside;return false;}
  auto classify=[&](const RoadPackagePlan::Box<Vec4>& box){
    const auto r=CoreBBox::frustumCheckAABB(planes,box.low,box.high,nullptr);
    return r==OUTSIDE_FRUSTUM?-1:r==IN_FRUSTUM?1:0;
  };
  auto mask=[&](const RoadPackagePlan::Box<Vec4>& box){
    return CoreBBox::activePlaneMaskAABB(clipObjectSpacePlanes,box.low,box.high,8);
  };
  unsigned selectionIndex=0,admitted=0;
  RoadPackagePlan::select(t.plan,classify,mask,[&](const auto&,RoadPackagePlan::Route r,unsigned bits){
    t.selected[selectionIndex++]={r,bits};if(r!=RoadPackagePlan::Route::Outside)++admitted;
  });
  // Preserve the ordinary zero-package path; do not manufacture a stale slot.
  if(!admitted)return false;
  if(RoadPrepared::audit){
    StaPipBagPackagesBBox boxes(b->vertices,b->count,t.capacity);
    packager.setMaxVertCount(t.capacity);packager.setRenderBBox(&boxes);
    packager.setObjectSpacePlanes(planes);packager.setClipObjectSpacePlanes(clipObjectSpacePlanes);
    u16 count=0;auto* ordinary=packager.create(&count,b,t.capacity);unsigned index=0;
    RoadPackagePlan::select(t.plan,classify,mask,[&](const auto& job,RoadPackagePlan::Route r,unsigned bits){
      const auto& p=ordinary[index++];
      const auto old=p.isInFrustum==OUTSIDE_FRUSTUM?RoadPackagePlan::Route::Outside:
        p.isInFrustum==IN_FRUSTUM?RoadPackagePlan::Route::Cull:
        p.guardBandOnly?RoadPackagePlan::Route::Guard:RoadPackagePlan::Route::Clip;
      TYRA_ASSERT(r==old && bits==p.clipPlaneMask && job.first==unsigned(p.vertices-b->vertices) &&
        job.count==p.size,"Road prepared live package admission differs");++RoadPrepared::checks;
    });
    TYRA_ASSERT(index==count,"Road prepared package loss");
    packager.setRenderBBox(nullptr);packager.setObjectSpacePlanes(nullptr);packager.setClipObjectSpacePlanes(nullptr);
  }
  if(!RoadPrepared::active)return false;
  if(!reuse){
    transformCacheValid=true;transformCacheModelPtr=b->info->model;transformCacheModel=t.model;
    transformCacheViewProj=view;transformCacheMvp=mvp;
  }
  for(u8 i=0;i<6;++i)transformCacheObjectSpacePlanes[i]=planes[i];transformCachePlanesValid=true;
  setMaxVertCount(t.capacity);qbufferRenderer.ensureProgramSet(false);
  qbufferRenderer.setSubmissionBatchCandidate(false,b->texture!=nullptr);
  RendererCoreTextureBuffers storage;RendererCoreTextureBuffers* textures=nullptr;
  if(b->texture){const auto tex=rendererCore->texture.useTexture(b->texture->texture);
    storage={tex.id,tex.core,tex.clut};textures=&storage;}
  const auto& wrap=b->texture && b->texture->texture->getWrapSettings()->horizontal!=WRAP_REPEAT?
    *b->texture->texture->getWrapSettings():RendererCoreGS::repeatWrap();
  qbufferRenderer.setBagWrap(&wrap);qbufferRenderer.clearLastProgramName();
  const bool receive=b->info->dynamicLightReceive && reception!=StaPipLightReception::Suppress;
  const RendererCoreSpotLight* light=nullptr;
  if(receive && b->info->dynLightPick){
    const auto& lo=t.bounds[0];const auto& hi=t.bounds[7];
    const Vec4 mid((lo.x+hi.x)*0.5F,(lo.y+hi.y)*0.5F,(lo.z+hi.z)*0.5F,1);
    const auto& m=t.model;const Vec4 center=m*mid;
    const float scale=Math::sqrtNonNegative(m.data[0]*m.data[0]+m.data[1]*m.data[1]+m.data[2]*m.data[2]);
    const float ex=hi.x-lo.x,ey=hi.y-lo.y,ez=hi.z-lo.z;
    light=rendererCore->pickDynLight(center,0.5F*Math::sqrtNonNegative(ex*ex+ey*ey+ez*ez)*scale,b->info->dynLightSkipSlot);
  }
  if(!receive || !b->info->spotLit)light=&kNoSpotLight;
  qbufferRenderer.setBagLight(light,&t.bounds);qbufferRenderer.setFogFacts(prim.fogging!=DRAW_ENABLE,&t.bounds);
  qbufferRenderer.setBagMayClip(true);qbufferRenderer.sendObjectData(b,&mvp,textures);
  qbufferRenderer.setClipperMVP(&mvp);
  for(unsigned index=0;index<t.packages.size();++index){
    auto& package=t.packages[index];const auto r=t.selected[index].route;
    const auto bits=t.selected[index].mask;
    auto emit=[&](RoadPrepared::Fragment& f){
      auto* buffer=qbufferRenderer.getBuffer();buffer->fillByPointer(b,f.first,f.sdk.count);
      buffer->stripped=!f.sdk.clip;buffer->nativeClipStrip=f.sdk.clip;
      buffer->nativeClipParity=f.sdk.clip?f.sdk.heads[0][2].words[3]&1:0;
      buffer->clipPlaneMask=f.sdk.clip?bits:0;
      buffer->coarseClipStrip=f.sdk.clip;buffer->partialStripOrigin=f.sdk.clip;
      buffer->roadFragment=&f;buffer->roadOwner=&t;
      if(f.sdk.clip)qbufferRenderer.clip(buffer);else qbufferRenderer.cull(buffer);
    };
    if(r==RoadPackagePlan::Route::Clip)for(auto& f:package.clips)emit(f);
    else if(r!=RoadPackagePlan::Route::Outside)emit(package.cull);
  }
  qbufferRenderer.flushBuffers();++RoadPrepared::partial;return true;
}
}
