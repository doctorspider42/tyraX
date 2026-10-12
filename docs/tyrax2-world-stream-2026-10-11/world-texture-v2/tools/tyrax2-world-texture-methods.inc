bool RendererCoreTexture::isWorldTexturePinned(u32 id) const {
  for(u32 pinned:worldTextureIds)if(pinned==id)return true;
  return false;
}

bool RendererCoreTexture::reserveWorldTextureSet(
    const std::vector<const Texture*>& textures,u32 wordBudget,u32 dynamicWords,
    WorldTextureFence fence,void* context) {
  if(!worldTextureIds.empty() || textures.empty() || textures.size()>64 ||
     !wordBudget || !dynamicWords || !fence ||
     Vif1Queue::recordingFrame() || worldTextureEpoch>=UINT64_MAX-1)return false;
  const u32 heap=static_cast<u32>(gs->vram.getHeapWords());
  if(dynamicWords>heap || wordBudget>heap-dynamicWords)return false;
  std::vector<u32> ids;
  std::vector<const Texture*> unique;
  u32 total=0;
  for(const auto* texture:textures){
    if(!texture || !texture->id || !texture->core || texture->vramResident)return false;
    bool duplicate=false;
    for(unsigned i=0;i<ids.size();++i)if(ids[i]==texture->id){
      if(unique[i]!=texture)return false;
      duplicate=true;
    }
    if(duplicate)continue;
    const int core=gs->vram.getSizeWords(*texture->core);
    const int clut=texture->clut && texture->clut->width>0 ?
        gs->vram.getSizeWords(*texture->clut):0;
    if(core<=0 || clut<0 || u32(core)>wordBudget-total ||
       u32(clut)>wordBudget-total-u32(core))return false;
    total+=u32(core)+u32(clut);ids.push_back(texture->id);unique.push_back(texture);
  }
  // This callback must perform an actual GPU drain, not a recorded barrier.
  fence(context);
  if(Vif1Queue::recordingFrame())return false;
  worldTextureIds=std::move(ids);
  for(const auto* texture:textures){
    if(!isResident(texture) && !tryMakeRoomFor(texture)){
      worldTextureIds.clear();return false;
    }
    auto slot=useTexture(texture);
    const int core=gs->vram.getAllocationWords(slot.core->address);
    const int clut=slot.clut ? gs->vram.getAllocationWords(slot.clut->address):0;
    if((slot.clut && !texture->clut) ||
       slot.core->psm!=texture->core->psm ||
       slot.core->info.width!=draw_log2(texture->getWidth()) ||
       slot.core->info.height!=draw_log2(texture->getHeight()) ||
       (slot.clut && slot.clut->psm!=texture->clut->psm) ||
       core!=gs->vram.getSizeWords(*texture->core) ||
       clut!=(texture->clut && texture->clut->width>0 ?
               gs->vram.getSizeWords(*texture->clut):0)){
      worldTextureIds.clear();return false;
    }
  }
  worldTextureFence=fence;worldTextureFenceContext=context;
  worldTextureWords=total;++worldTextureEpoch;
  return true;
}

RendererCoreTextureBuffers RendererCoreTexture::worldTextureSlot(u32 id) {
  if(!isWorldTexturePinned(id))return {0,nullptr,nullptr,0};
  return getAllocatedBuffersByTextureId(id);
}

void RendererCoreTexture::releaseWorldTextureSet() {
  if(worldTextureIds.empty())return;
  TYRA_ASSERT(!Vif1Queue::recordingFrame() && worldTextureFence,
              "A44 world textures require an outside-recording GS fence");
  worldTextureFence(worldTextureFenceContext);
  TYRA_ASSERT(!Vif1Queue::recordingFrame(),"A44 release fence entered recording");
  worldTextureIds.clear();worldTextureWords=0;
  worldTextureFence=nullptr;worldTextureFenceContext=nullptr;++worldTextureEpoch;
}

bool RendererCoreTexture::diagnosticWorldVictimCheck() {
  if(worldTextureIds.empty())return false;
  const u32 previousStart=prevFrameStartSeq;
  std::vector<u32> previous;
  for(const auto& allocation:currentAllocations)previous.push_back(allocation.lastUsedSeq);
  bool valid=true;
  // Exercise both actual victim tiers, making protected entries the best victim.
  for(unsigned tier=0;tier<2;++tier){
    prevFrameStartSeq=tier?0:2;
    for(auto& allocation:currentAllocations)
      allocation.lastUsedSeq=isWorldTexturePinned(allocation.id)?(tier?2:0):1;
    const int victim=pickVictim();
    if(victim>=0 && isWorldTexturePinned(currentAllocations[victim].id))valid=false;
  }
  for(unsigned i=0;i<currentAllocations.size();++i)
    currentAllocations[i].lastUsedSeq=previous[i];
  prevFrameStartSeq=previousStart;
  return valid;
}
