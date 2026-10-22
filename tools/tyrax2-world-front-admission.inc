namespace Tyra {
unsigned StaPipCore::verifyWorldTicketAdmission(StaPipBag* b){
  if(!WorldFront::active || !ticketShape(b) || !b->worldTicket.value)return 0;
  auto& t=*b->worldTicket.value;
  if(t.vertices!=b->vertices || t.colors!=b->color->many || t.count!=b->count ||
      t.run!=b->packageSize || t.bboxVersion!=b->bboxVersion || t.epoch!=getWorldEpoch() ||
      t.bufferSize!=qbufferRenderer.getBufferSize() || t.textured!=(b->texture!=nullptr) ||
      t.coordinates!=(b->texture?b->texture->coordinates:nullptr) ||
      t.stamps[0]!=*b->contentVersion || t.stamps[1]!=*b->color->contentVersion ||
      t.stamps[2]!=(b->texture?*b->texture->contentVersion:0) ||
      memcmp(t.model.data,b->info->model->data,sizeof(t.model.data)))return 0;
  unsigned checks=0;
  const u32 head=qbufferRenderer.diagnosticObjectHeadQwords();
  auto reject=[&](){
    TYRA_ASSERT(!renderWorldTicket(b,StaPipLightReception::FromInfo),"A35 stale ticket admitted");
    TYRA_ASSERT(qbufferRenderer.diagnosticObjectHeadQwords()==head,"A35 rejected ticket emitted a head");
    ++checks;
  };
  auto changed=[&](auto& value,auto replacement){auto saved=value;value=replacement;reject();value=saved;};
  changed(b->vertices,b->vertices+1);changed(b->count,b->count+1);
  changed(b->packageSize,0U);changed(b->bboxVersion,b->bboxVersion^1U);
  changed(b->stripped,false);changed(b->contentVersion,nullptr);
  changed(b->info->fullClipChecks,false);changed(b->info->model,nullptr);
  changed(b->info->transformationType,TyraMP);
  changed(b->info->frustumCulling,PipelineInfoBagFrustumCulling_None);
  changed(b->color->many,b->color->many+1);
  changed(b->color->single,reinterpret_cast<decltype(b->color->single)>(b->color->many));
  changed(b->color->contentVersion,nullptr);changed(b->color,nullptr);
  changed(b->lighting,reinterpret_cast<decltype(b->lighting)>(b));
  changed(b->billboard,reinterpret_cast<decltype(b->billboard)>(b));
  changed(t.epoch,t.epoch^1U);changed(t.bufferSize,t.bufferSize^1U);
  changed(t.stamps[0],t.stamps[0]^1U);changed(t.stamps[1],t.stamps[1]^1U);
  for(unsigned i=0;i<16;++i)changed(b->info->model->data[i],b->info->model->data[i]+1.0F);
  if(b->texture){
    changed(b->texture->coordinates,b->texture->coordinates+1);
    changed(b->texture->coordinatesAreNormals,true);
    changed(b->texture->contentVersion,nullptr);
    changed(t.stamps[2],t.stamps[2]^1U);changed(b->texture,nullptr);
  }
  changed(b->info,nullptr);changed(WorldFront::active,false);
  auto ticket=std::move(b->worldTicket.value);reject();b->worldTicket.value=std::move(ticket);
  return checks;
}
}
