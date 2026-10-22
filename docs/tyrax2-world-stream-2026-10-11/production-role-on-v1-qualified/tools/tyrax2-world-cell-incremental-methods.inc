namespace Vehicle_playground {
void TerrainGame::releaseCompleteTerrain(){
  WorldCellComplete::active=false;
  for(auto& ch:terrainChunks)ch.completeCell.owner.reset();
  stapip.core.collectWorldOwners();
  if(completeTerrainSlots){
    engine->renderer.core.texture.releaseWorldTextureSet();
    completeTerrainSlots=false;
  }
  completeTerrainDirty=true;
}

bool TerrainGame::compileCompleteTerrainChunk(TerrainChunk& ch){
  auto& core=engine->renderer.core;
  u32 depth;memcpy(&depth,&Tyra::RendererCoreDepth::scale,4);
  if(!completeTerrainSlots || core.spot.enabled || core.dynLightCount ||
     completeTerrainTextureEpoch!=core.texture.getWorldTextureEpoch() ||
     completeTerrainProgramEpoch!=stapip.core.getWorldEpoch() ||
     completeTerrainDepth!=depth || completeTerrainFog!=core.fog.enabled)return false;
  std::vector<Tyra::StaPipBag*> ordered;
  auto append=[&](Tyra::StaPipBag* b){if(b && b->count)ordered.push_back(b);};
  append(ch.bag.get());for(auto& layer:ch.layerPasses)append(layer.bag.get());
  append(ch.aoBag.get());append(ch.gsBag.get());append(ch.emisBag.get());
  if(ordered.empty())return false;
  for(auto* b:ordered){
    if(b->texture && (!b->texture->texture ||
       !core.texture.worldTextureSlot(b->texture->texture->id).id))return false;
  }
  // Old bodies retain their queue readers; this is not a GS texture release.
  for(auto* b:ordered)b->worldOwner.reset();
  stapip.core.collectWorldOwners();
  return stapip.core.compileCompleteWorldCell(ch.completeCell,ordered);
}

bool TerrainGame::prepareCompleteTerrain(){
  auto& core=engine->renderer.core;
  if(core.spot.enabled || core.dynLightCount)return false;
  u32 depth;memcpy(&depth,&Tyra::RendererCoreDepth::scale,4);
  if(completeTerrainSlots && completeTerrainTextureEpoch==core.texture.getWorldTextureEpoch() &&
     completeTerrainProgramEpoch==stapip.core.getWorldEpoch() &&
     completeTerrainDepth==depth && completeTerrainFog==core.fog.enabled){
    if(!completeTerrainDirty)return true;
    unsigned rebuilt=0;
    for(auto& ch:terrainChunks){
      if(ch.cx<0 || !ch.bag || !ch.bag->count || ch.completeCell.owner)continue;
      if(!compileCompleteTerrainChunk(ch))return false;
      ++rebuilt;
    }
    completeTerrainDirty=false;
    printf("A44LOCAL preRecord=1 rebuilt=%u epoch=%llu\n",rebuilt,
           static_cast<unsigned long long>(completeTerrainTextureEpoch));fflush(stdout);
    return true;
  }
  releaseCompleteTerrain();
  std::vector<const Tyra::Texture*> textures;
  auto addTexture=[&](const Tyra::Texture* t){
    if(t && std::find(textures.begin(),textures.end(),t)==textures.end())textures.push_back(t);
  };
  auto add=[&](Tyra::StaPipBag* b){
    if(b && b->count && b->texture)addTexture(b->texture->texture);
  };
  for(auto& ch:terrainChunks){
    if(ch.cx<0 || !ch.bag || !ch.bag->count)continue;
    add(ch.bag.get());for(auto& layer:ch.layerPasses)add(layer.bag.get());
    add(ch.aoBag.get());add(ch.gsBag.get());add(ch.emisBag.get());
  }
  // These masks already have scene-owned EE references, including future ring cells.
  for(const auto& path:groundShadowPins){
    const auto it=texCache.find(path);
    if(it==texCache.end() || !it->second.tex)return false;
    addTexture(it->second.tex);
  }
  if(textures.empty())return false;
  auto fence=[](void* p){auto* c=static_cast<Tyra::RendererCore*>(p);
    c->synchronizeFrame();c->sync.align3D();c->sync.align2D();};
  const unsigned heap=core.gs.vram.getHeapWords();
  if(!core.texture.reserveWorldTextureSet(textures,heap*3/4,heap/4,fence,&core))return false;
  completeTerrainSlots=true;
  completeTerrainTextureEpoch=core.texture.getWorldTextureEpoch();
  completeTerrainProgramEpoch=stapip.core.getWorldEpoch();
  completeTerrainDepth=depth;completeTerrainFog=core.fog.enabled;
  unsigned cells=0,passes=0;
  for(auto& ch:terrainChunks){
    if(ch.cx<0 || !ch.bag || !ch.bag->count)continue;
    if(!compileCompleteTerrainChunk(ch)){releaseCompleteTerrain();return false;}
    ++cells;passes+=1+ch.layerPasses.size()+unsigned(bool(ch.aoBag))+
      unsigned(bool(ch.gsBag))+unsigned(bool(ch.emisBag));
  }
  completeTerrainDirty=false;
  printf("A44TERRAIN compile=PASS cells=%u passes=%u textures=%u words=%u heap=%u\n",
    cells,passes,unsigned(textures.size()),unsigned(core.texture.getWorldTextureWords()),heap);
  fflush(stdout);return cells>0;
}
}
