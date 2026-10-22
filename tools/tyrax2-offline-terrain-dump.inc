namespace Vehicle_playground {
void TerrainGame::dumpOfflineTerrain(){
  auto& core=engine->renderer.core;
  core.synchronizeFrame();core.sync.align3D();core.sync.align2D();
  TYRA_ASSERT(!completeTerrainSlots,"Offline dump before world cell ownership");
  TYRA_ASSERT(g_activeScene==0 && TERRAIN_LAYER_COUNT==0 && !SCENE_PROBES && !SCENE_EMIS &&
    !terrainMapLit && !SCENE_AO_MAP_LIT && !SCENE_AO_MAP_GI && !SCENE_AO_MAP_GILUM,
    "Offline dump static daylight contract");
  FILE* file=fopen("host:offline-terrain-source.bin","wb");
  TYRA_ASSERT(file,"Offline dump output");
  auto word=[&](unsigned value){TYRA_ASSERT(fwrite(&value,4,1,file)==1,"Offline dump write");};
  word(0x3158544f);word(1);word(g_activeScene);word(terrainChunksX);word(terrainChunksZ);
  unsigned records=0,total=0;
  OfflineTerrain::forcing=true;
  for(int cz=0;cz<terrainChunksZ;++cz)for(int cx=0;cx<terrainChunksX;++cx){
    const int slot=0;
    for(int own: {1,2,4}){
      OfflineTerrain::own=own;
      int choices[4][3],counts[4];
      const bool boundary[4]={cx==0,cx==terrainChunksX-1,cz==0,cz==terrainChunksZ-1};
      for(int e=0;e<4;++e){counts[e]=1;choices[e][0]=own;
        if(!boundary[e])for(int n:{2,4})if(n>own)choices[e][counts[e]++]=n;}
      for(int a=0;a<counts[0];++a)for(int b=0;b<counts[1];++b)
      for(int c=0;c<counts[2];++c)for(int d=0;d<counts[3];++d){
        OfflineTerrain::edges[0]=choices[0][a];OfflineTerrain::edges[1]=choices[1][b];
        OfflineTerrain::edges[2]=choices[2][c];OfflineTerrain::edges[3]=choices[3][d];
        buildTerrainChunk(slot,cx,cz);
        const auto& ch=terrainChunks[slot];
        TYRA_ASSERT(ch.layerPasses.empty() && !ch.aoBag && !ch.emisBag,"Offline dump unexpected pass");
        word(0x52454331);word(cx);word(cz);word(own);
        for(int e=0;e<4;++e)word(OfflineTerrain::edges[e]);
        const float bounds[]={ch.minY,ch.maxY,ch.aabbMin[0],ch.aabbMin[1],ch.aabbMin[2],ch.aabbMax[0],ch.aabbMax[1],ch.aabbMax[2]};
        TYRA_ASSERT(fwrite(bounds,sizeof(bounds),1,file)==1,"Offline bounds write");
        auto array=[&](const auto& data){
          const unsigned size=data.size()*sizeof(*data.data());word(size);total+=size;
          TYRA_ASSERT(!size || fwrite(data.data(),size,1,file)==1,"Offline array write");
        };
        array(ch.vertices);array(ch.colors);array(ch.sts);
        array(ch.gsVerts);array(ch.gsCols);array(ch.gsSts);
        ++records;
      }
    }
  }
  word(0x454e4431);word(records);word(total);
  TYRA_ASSERT(fclose(file)==0,"Offline dump close");
  OfflineTerrain::forcing=false;
  printf("OFFLINEDUMPDONE records=%u bytes=%u scene=%u native=1 gpuExecuted=0\n",records,total,unsigned(g_activeScene));fflush(stdout);
  SleepThread();
}
}
