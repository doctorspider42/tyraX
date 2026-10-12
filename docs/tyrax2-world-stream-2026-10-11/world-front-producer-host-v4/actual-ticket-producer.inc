namespace Tyra {
bool StaPipCore::prepareWorldTicket(StaPipBag* b){
  if((!WorldFront::active && !WorldRuntime::cold) || !ticketShape(b) ||
      !qbufferRenderer.isVU1ClippingEnabled() || qbufferRenderer.hasProgramOverrides() ||
      rendererCore->blss.wantsProxies() || WorldFront::live>=WorldFront::OwnerLimit)return false;
  const u32 capacity=getMaxVertCountByBag(b);
  try {
    auto* boxes=cacher.getBBoxes(b->vertices,b->count,static_cast<u32>(reinterpret_cast<std::uintptr_t>(b->vertices)),b->bboxVersion,capacity);
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

}
