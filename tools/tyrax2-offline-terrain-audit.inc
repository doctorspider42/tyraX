namespace Vehicle_playground {
void TerrainGame::auditOfflineTerrain(){
  auto& core=engine->renderer.core;core.synchronizeFrame();core.sync.align3D();core.sync.align2D();
  TYRA_ASSERT(!completeTerrainSlots && g_activeScene==0,"Offline oracle before cell preparation");
  const bool saved=OfflineTerrain::enabled;unsigned checks=0,bytes=0;
  auto snapshot=[](const TerrainChunk& ch){
    std::vector<unsigned char> out;
    auto add=[&](const void* p,unsigned n){const auto* b=static_cast<const unsigned char*>(p);if(n)out.insert(out.end(),b,b+n);};
    auto array=[&](const auto& a){const unsigned n=a.size()*sizeof(*a.data());add(&n,4);add(a.data(),n);};
    array(ch.vertices);array(ch.colors);array(ch.sts);array(ch.gsVerts);
    if(!ch.gsVerts.empty())array(ch.gsCols);else {const unsigned zero=0;add(&zero,4);}
    array(ch.gsSts);add(&ch.minY,4);add(&ch.maxY,4);add(ch.aabbMin,12);add(ch.aabbMax,12);
    auto bag=[&](const Tyra::StaPipBag* b){const bool present=b;add(&present,sizeof(present));if(!b)return;
      add(&b->count,sizeof(b->count));add(&b->packageSize,sizeof(b->packageSize));add(&b->stripped,sizeof(b->stripped));add(&b->refineStrips,sizeof(b->refineStrips));
      add(&b->info,sizeof(b->info));add(&b->lighting,sizeof(b->lighting));
      const bool tex=b->texture;add(&tex,sizeof(tex));if(tex)add(&b->texture->texture,sizeof(b->texture->texture));};
    bag(ch.bag.get());bag(ch.gsBag.get());bag(ch.aoBag.get());bag(ch.emisBag.get());
    TYRA_ASSERT(ch.layerPasses.empty(),"Offline oracle extra layer");return out;
  };
  for(int slot=0;slot<int(terrainChunks.size());++slot){auto& ch=terrainChunks[slot];if(ch.cx<0)continue;
    const int cx=ch.cx,cz=ch.cz;OfflineTerrain::enabled=false;buildTerrainChunk(slot,cx,cz);const auto expected=snapshot(ch);
    OfflineTerrain::enabled=true;buildTerrainChunk(slot,cx,cz);const auto actual=snapshot(ch);
    if(actual!=expected){
      printf("OFFLINEMISMATCH cx=%d cz=%d lod=%u expected=%u actual=%u\n",cx,cz,unsigned(ch.lod),unsigned(expected.size()),unsigned(actual.size()));
      unsigned reported=0;
      for(unsigned i=0;i<expected.size() && i<actual.size() && reported<12;++i)if(expected[i]!=actual[i]){
        printf("OFFLINEBYTE offset=%u expected=%u actual=%u\n",i,unsigned(expected[i]),unsigned(actual[i]));++reported;
      }
      auto dump=[&](const char* path,const std::vector<unsigned char>& bytes){FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"Offline mismatch output");TYRA_ASSERT(fwrite(bytes.data(),bytes.size(),1,f)==1,"Offline mismatch write");fclose(f);};
      dump("host:offline-mismatch-expected.bin",expected);dump("host:offline-mismatch-actual.bin",actual);fflush(stdout);
    }
    TYRA_ASSERT(actual==expected,"Offline terrain native producer mismatch");
    OfflineTerrain::enabled=saved;buildTerrainChunk(slot,cx,cz);TYRA_ASSERT(snapshot(ch)==expected,"Offline terrain restored producer mismatch");
    ++checks;bytes+=expected.size();
  }
  OfflineTerrain::enabled=saved;OfflineTerrain::restores=OfflineTerrain::fallbacks=0;
  printf("OFFLINEORACLE phase=%u checks=%u bytes=%u exact=1 outsidePrice=1\n",MotionProbe::phase,checks,bytes);fflush(stdout);
}
}
