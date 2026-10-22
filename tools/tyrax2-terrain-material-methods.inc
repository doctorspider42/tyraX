namespace Vehicle_playground {
void TerrainGame::prepareComposedTerrain(){
  if(MaterialCompose::generation==sceneGeneration)return;
  auto& core=engine->renderer.core;core.synchronizeFrame();core.sync.align3D();core.sync.align2D();
  TYRA_ASSERT(!completeTerrainSlots && g_activeScene==0,"Material preparation before ownership");
  unsigned loaded=0;
  for(unsigned i=0;i<25;++i){
    const char* path=MaterialCompose::paths[i];
    MaterialCompose::textures[i]=path?acquireTexture(path):nullptr;
    TYRA_ASSERT(!path || MaterialCompose::textures[i],"Composed material load failed");
    if(MaterialCompose::textures[i]){MaterialCompose::textures[i]->setWrapSettings(Tyra::Clamp,Tyra::Clamp);++loaded;}
  }
  MaterialCompose::generation=sceneGeneration;
  printf("MATERIALLOAD generation=%u textures=%u resolution=128 qualityEquivalent=0\n",sceneGeneration,loaded);fflush(stdout);
}
void TerrainGame::applyComposedTerrain(TerrainChunk& ch){
  if(!MaterialCompose::enabled || MaterialCompose::generation!=sceneGeneration || g_activeScene!=0 || !ch.gsBag)return;
  TYRA_ASSERT(ch.cx>=0 && ch.cx<5 && ch.cz>=0 && ch.cz<5,"Composed terrain coordinate");
  Tyra::Texture* texture=MaterialCompose::textures[ch.cz*5+ch.cx];
  TYRA_ASSERT(texture && ch.bag && ch.bag->texture && ch.layerPasses.empty() && !ch.aoBag && !ch.emisBag,"Composed material fixture contract");
  const float spanX=TERRAIN_CHUNK_CELLS*(TERRAIN_WIDTH/(HM_W-1));
  const float spanZ=TERRAIN_CHUNK_CELLS*(TERRAIN_DEPTH/(HM_D-1));
  const float ox=-TERRAIN_WIDTH*.5F+ch.cx*spanX,oz=-TERRAIN_DEPTH*.5F+ch.cz*spanZ;
  ch.sts.resize(ch.vertices.size());
  const auto& vertices=ch.vertices;
  for(unsigned i=0;i<ch.sts.size();++i){const auto& v=vertices[i];ch.sts[i]=Tyra::Vec4((v.x-ox)/spanX,(v.z-oz)/spanZ,1,0);}
  ch.texBag.texture=texture;ch.sts.bind(&ch.texBag);ch.bag->texture=&ch.texBag;
  ++MaterialCompose::applied;MaterialCompose::removedVertices+=ch.gsBag->count;
  ch.gsBag.reset();
}
}
