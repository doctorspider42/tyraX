namespace Vehicle_playground {
void TerrainGame::captureCompleteWorldPriceCheckpoint(){
  auto& core=engine->renderer.core;
  const float dt=g_frameDt;g_frameDt=0;
  const bool orderActive=WorldHeldOrder::active,order=WorldHeldOrder::interleaved;
  WorldHeldOrder::active=true;WorldHeldOrder::interleaved=true;
  const bool movingCandidate=WorldMoving::enabled;
  for(unsigned arm=0;arm<3;++arm){
  const bool candidate=arm==1?!movingCandidate:movingCandidate;
  core.synchronizeFrame();core.sync.align2D();
  releaseCompleteTerrain();
  if(candidate)TYRA_ASSERT(prepareCompleteTerrain(),"A44 price checkpoint preparation");
  else {
    for(auto& ch:terrainChunks){
      if(ch.cx<0 || !ch.bag || !ch.bag->count)continue;
      stapip.core.prepareWorldGeometry(ch.bag.get());
      for(auto& layer:ch.layerPasses)stapip.core.prepareWorldGeometry(layer.bag.get());
      stapip.core.prepareWorldGeometry(ch.aoBag.get());stapip.core.prepareWorldGeometry(ch.gsBag.get());
      stapip.core.prepareWorldGeometry(ch.emisBag.get());
    }
  }
  WorldCellComplete::active=candidate;
  for(unsigned i=0;i<32;++i){
    core.synchronizeFrame();
    TYRA_ASSERT(core.gs.selectDiagnosticTarget(0),"A44 price held target");
    beamBatchCall=0;
    if(i==31)WorldCellComplete::requested=WorldCellComplete::submitted=WorldCellComplete::fallback=0;
    engine->renderer.beginFrame(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
    engine->renderer.renderer3D.usePipeline(stapip);
    renderScene();engine->renderer.endFrame();core.synchronizeFrame();core.sync.align2D();
  }
  for(unsigned kind=0;kind<3;++kind){
    auto* fb=kind==2?core.gs.getPreviousFrameBuffer():core.gs.getPreviousRealFrameBuffer();
    unsigned address=fb->address,width=fb->width,height=fb->height,psm=fb->psm;
    if(kind==1){auto* t=core.envMap.getTexture()->vramResident;
      TYRA_ASSERT(t,"A44 price missing env");address=t->address;width=height=128;psm=t->psm;}
    TYRA_ASSERT(width<=1024 && psm==(kind==2?2:0),"A44 price target format");
    TYRA_ASSERT(core.gs.getFrameBufferCount()==2,"A44 price Hybrid2 count");
    const unsigned stage=MotionProbe::phase*3+arm;
    char path[80];snprintf(path,sizeof(path),"host:world-price-%u-%s.bin",stage,
      kind==2?"display":kind==1?"env":"main");
    FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"A44 price capture open");
    unsigned h[4]={width,height,psm,stage};
    TYRA_ASSERT(fwrite(h,sizeof(h),1,f)==1,"A44 price header");
    alignas(16) static unsigned line[1024];
    for(unsigned y=0;y<height;++y){
      TYRA_ASSERT(ps2_screenshot(line,address/64,0,y,width,1,psm),"A44 price readback");
      FlushCache(0);TYRA_ASSERT(fwrite(line,psm==2?2:4,width,f)==width,"A44 price pixels");
    }
    TYRA_ASSERT(fclose(f)==0,"A44 price close");
  }
  printf("A44PRICEIMAGE phase=%u arm=%u candidate=%u requested=%u submitted=%u fallback=%u order=2 complete=1\n",
    MotionProbe::phase,arm,unsigned(candidate),WorldCellComplete::requested,WorldCellComplete::submitted,WorldCellComplete::fallback);
  fflush(stdout);
  }
  WorldCellComplete::active=movingCandidate;
  WorldHeldOrder::active=orderActive;WorldHeldOrder::interleaved=order;g_frameDt=dt;
}
}
