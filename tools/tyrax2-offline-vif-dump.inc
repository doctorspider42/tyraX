namespace Vehicle_playground {
void TerrainGame::dumpOfflineVif(){
  auto& core=engine->renderer.core;
  core.synchronizeFrame();core.sync.align3D();core.sync.align2D();
  OfflineTerrain::enabled=true;
  TYRA_ASSERT(prepareCompleteTerrain(),"Offline VIF bootstrap texture reservation");
  FILE* file=fopen("host:offline-vif-source.bin","wb");TYRA_ASSERT(file,"Offline VIF output");
  auto word=[&](unsigned v){TYRA_ASSERT(fwrite(&v,4,1,file)==1,"Offline VIF write");};
  word(0x31464956);word(1);word(terrainChunksX);word(terrainChunksZ);
  unsigned records=0,total=0;
  OfflineTerrain::forcing=true;
  for(const auto& entry:OfflineTerrain::entries){
    OfflineTerrain::own=entry.lod;
    for(const auto& variant:entry.variants){
      memcpy(OfflineTerrain::edges,variant.edges,16);
      std::vector<OfflineVifEngine::Body> geometry;std::vector<unsigned> metadata;
      OfflineVifEngine::capture=&geometry;OfflineVifEngine::metadata=&metadata;
      buildOfflineTerrainChunk(0,entry.cx,entry.cz);
      OfflineVifEngine::capture=nullptr;OfflineVifEngine::metadata=nullptr;
      TYRA_ASSERT(!geometry.empty() && geometry.size()<=2 && metadata.size()==geometry.size()*5,"Offline VIF pass shape");
      word(0x52454331);word(entry.cx);word(entry.cz);word(entry.lod);
      for(auto edge:variant.edges)word(edge);
      word(geometry.size());for(auto value:metadata)word(value);
      for(const auto& stream:geometry){const unsigned size=stream.size()*16;word(size);total+=size;
        TYRA_ASSERT(fwrite(stream.data(),size,1,file)==1,"Offline VIF stream write");}
      ++records;
    }
  }
  word(0x454e4431);word(records);word(total);TYRA_ASSERT(fclose(file)==0,"Offline VIF close");
  printf("VIFDUMPDONE records=%u bytes=%u native=1 gpuExecuted=0\n",records,total);fflush(stdout);
  SleepThread();
}
}
