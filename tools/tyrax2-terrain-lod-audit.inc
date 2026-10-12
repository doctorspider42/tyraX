namespace Vehicle_playground {
void TerrainGame::auditTerrainLodKey(){
  auto& core=engine->renderer.core;
  core.synchronizeFrame();core.sync.align3D();core.sync.align2D();
  TYRA_ASSERT(!completeTerrainSlots,"LOD audit must precede compiled owner preparation");
  TYRA_ASSERT(g_activeScene<2 && TERRAIN_LAYER_COUNT==0 && !SCENE_PROBES && !SCENE_EMIS &&
    !terrainMapLit && !SCENE_AO_MAP_LIT && !SCENE_AO_MAP_GI && !SCENE_AO_MAP_GILUM &&
    g_emisLocal.empty(),"LOD audit fixture shading guard");
  auto snapshot=[](const TerrainChunk& ch){
    std::vector<unsigned char> out;
    auto bytes=[&](const void* ptr,size_t size){
      if(!size)return;const auto* b=static_cast<const unsigned char*>(ptr);out.insert(out.end(),b,b+size);
    };
    auto array=[&](const auto& a){const unsigned n=a.size();bytes(&n,sizeof(n));bytes(a.data(),n*sizeof(*a.data()));};
    array(ch.vertices);array(ch.colors);array(ch.sts);
    array(ch.gsVerts);array(ch.gsCols);array(ch.gsSts);
    array(ch.aoSts);array(ch.aoCols);array(ch.emisCols);
    bytes(&ch.minY,sizeof(ch.minY));bytes(&ch.maxY,sizeof(ch.maxY));
    bytes(ch.aabbMin,sizeof(ch.aabbMin));bytes(ch.aabbMax,sizeof(ch.aabbMax));
    auto bag=[&](const Tyra::StaPipBag* b){
      const bool exists=b;bytes(&exists,sizeof(exists));if(!b)return;
      bytes(&b->count,sizeof(b->count));bytes(&b->packageSize,sizeof(b->packageSize));
      bytes(&b->stripped,sizeof(b->stripped));bytes(&b->refineStrips,sizeof(b->refineStrips));
      bytes(&b->info,sizeof(b->info));bytes(&b->lighting,sizeof(b->lighting));
      const bool textured=b->texture;bytes(&textured,sizeof(textured));
      if(textured)bytes(&b->texture->texture,sizeof(b->texture->texture));
    };
    bag(ch.bag.get());bag(ch.gsBag.get());bag(ch.aoBag.get());bag(ch.emisBag.get());
    TYRA_ASSERT(ch.layerPasses.empty(),"LOD audit unexpected layers");return out;
  };
  unsigned checks=0,shadowChecks=0,lodMask=0,totalBytes=0;
  for(int slot=0;slot<int(terrainChunks.size());++slot){
    auto& ch=terrainChunks[slot];if(ch.cx<0)continue;
    const int cx=ch.cx,cz=ch.cz;
    buildTerrainChunk(slot,cx,cz);
    const TerrainLodKey::Key raw={ch.lod,ch.lodW,ch.lodE,ch.lodN,ch.lodS};
    const auto expected=snapshot(ch);
    auto other=raw;
    auto change=[&](int n,bool boundary){return boundary?4:(n<=raw.lod?(n==1?raw.lod:1):n);};
    other.lodW=change(raw.lodW,cx==0);other.lodE=change(raw.lodE,cx==terrainChunksX-1);
    other.lodN=change(raw.lodN,cz==0);other.lodS=change(raw.lodS,cz==terrainChunksZ-1);
    TYRA_ASSERT(TerrainLodKey::equivalent(raw,other,cx,cz,terrainChunksX,terrainChunksZ),"LOD audit effective key");
    TerrainLodPolicy::forced=other;TerrainLodPolicy::forcing=true;
    buildTerrainChunk(slot,cx,cz);TerrainLodPolicy::forcing=false;
    TYRA_ASSERT(snapshot(ch)==expected,"LOD audit native full producer bytes changed");
    buildTerrainChunk(slot,cx,cz);
    TYRA_ASSERT(snapshot(ch)==expected,"LOD audit restored producer bytes changed");
    ++checks;shadowChecks+=ch.gsBag && ch.gsBag->count;lodMask|=1U<<ch.lod;totalBytes+=expected.size();
  }
  TYRA_ASSERT(checks>0 && shadowChecks>0,"LOD audit missing terrain/shadow producer");
  TerrainLodPolicy::qualified=true;
  printf("LODORACLE phase=%u checks=%u shadows=%u lodMask=%u bytes=%u exact=1 outsidePrice=1\n",
    MotionProbe::phase,checks,shadowChecks,lodMask,totalBytes);fflush(stdout);
}
}
