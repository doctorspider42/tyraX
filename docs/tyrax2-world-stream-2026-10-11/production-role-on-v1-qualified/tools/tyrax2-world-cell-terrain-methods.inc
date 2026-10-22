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

bool TerrainGame::prepareCompleteTerrain(){
  auto& core=engine->renderer.core;
  if(core.spot.enabled || core.dynLightCount)return false;
  if(!completeTerrainDirty && completeTerrainSlots)return true;
  releaseCompleteTerrain();
  std::vector<const Tyra::Texture*> textures;
  auto add=[&](Tyra::StaPipBag* b){
    if(!b || !b->count || !b->texture)return;
    const auto* t=b->texture->texture;
    if(std::find(textures.begin(),textures.end(),t)==textures.end())textures.push_back(t);
  };
  for(auto& ch:terrainChunks){
    if(ch.cx<0 || !ch.bag || !ch.bag->count)continue;
    add(ch.bag.get());for(auto& layer:ch.layerPasses)add(layer.bag.get());
    add(ch.aoBag.get());add(ch.gsBag.get());add(ch.emisBag.get());
  }
  if(textures.empty())return false;
  auto fence=[](void* p){auto* c=static_cast<Tyra::RendererCore*>(p);
    c->synchronizeFrame();c->sync.align3D();c->sync.align2D();};
  const unsigned heap=core.gs.vram.getHeapWords();
  if(!core.texture.reserveWorldTextureSet(textures,heap*3/4,heap/4,fence,&core)){
    printf("A44TERRAIN reservation=FAIL textures=%u heap=%u\n",unsigned(textures.size()),heap);
    fflush(stdout);return false;
  }
  completeTerrainSlots=true;
  // Replace old terrain body ownership, not retain two full copies indefinitely.
  for(auto& ch:terrainChunks){
    auto retire=[](Tyra::StaPipBag* b){if(b)b->worldOwner.reset();};
    retire(ch.bag.get());for(auto& layer:ch.layerPasses)retire(layer.bag.get());
    retire(ch.aoBag.get());retire(ch.gsBag.get());retire(ch.emisBag.get());
  }
  stapip.core.collectWorldOwners();
  unsigned cells=0,passes=0;
  for(auto& ch:terrainChunks){
    if(ch.cx<0 || !ch.bag || !ch.bag->count)continue;
    std::vector<Tyra::StaPipBag*> ordered;
    auto append=[&](Tyra::StaPipBag* b){if(b && b->count)ordered.push_back(b);};
    append(ch.bag.get());for(auto& layer:ch.layerPasses)append(layer.bag.get());
    append(ch.aoBag.get());append(ch.gsBag.get());append(ch.emisBag.get());
    if(!stapip.core.compileCompleteWorldCell(ch.completeCell,ordered)){
      printf("A44TERRAIN compile=FAIL cx=%d cz=%d cells=%u passes=%u\n",ch.cx,ch.cz,cells,passes);
      fflush(stdout);releaseCompleteTerrain();return false;
    }
    ++cells;passes+=ordered.size();
  }
  completeTerrainDirty=false;
  printf("A44TERRAIN compile=PASS cells=%u passes=%u textures=%u words=%u heap=%u\n",
    cells,passes,unsigned(textures.size()),unsigned(core.texture.getWorldTextureWords()),heap);
  fflush(stdout);return cells>0;
}
}
