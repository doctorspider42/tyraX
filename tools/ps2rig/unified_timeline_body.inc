// One owner of the simulation interval; detectors and response remain legacy.
void TerrainGame::stepUnifiedTimeline(float frameDt) {
  if(CB::finished || !CB::ready)return;
  const unsigned begin=CB::ticks();
  UnifiedTimeline::enabled=!CB::baseline;
  // Preserve the old vehicle API's conversion rounding at its adapter. It
  // denotes the same interval, but scale*(1/50) need not equal g_frameDt
  // bitwise. Object gravity must continue to receive the original dt.
  const float vehicleDt=g_frameScale*(1.0F/50.0F);
  int n=int(vehicleDt*50.0F+0.5F);if(n<1)n=1;if(n>4)n=4;
  const float h=frameDt/float(n);
  const float vehicleH=vehicleDt/float(n);
  UnifiedTimeline::Row row{unsigned(CB::frame),unsigned(n),0,0,0,0,0,0,frameDt,h};
  row.enabled=UnifiedTimeline::enabled;
  for(const auto& o:runtimeObjects)if(o.active && o.data.physics)++row.bodies;
  for(int i=0;i<vehicleCount_;++i)if(vehicles_[i].active)++row.cars;
  if(!UnifiedTimeline::enabled){
    updateObjectPhysics();++row.physics;updateSpinners();broadPhaseInvalidate();
    stepVehicles(vehicleDt);row.vehicles=unsigned(n);
  }else{
    const float savedDt=g_frameDt,savedScale=g_frameScale;
    // Script-visible velocities/spin are frame displacements. During this
    // block every physics/vehicle impulse is expressed in step displacement.
    const float ratio=1.0F/float(n);
    UnifiedTimeline::unitScale=ratio;
    UnifiedTimeline::begin(runtimeObjects,sceneGeneration,frameDt);
    if(n>1)for(auto& o:runtimeObjects)if(o.data.physics){
      o.velocityX*=ratio;o.velocityY*=ratio;o.velocityZ*=ratio;
      for(float& spin:o.spin)spin*=ratio;
    }
    vehFrameDt_=vehicleDt;
    for(VehGatherCache& cache:vehGather_)cache.valid=false;
    for(int k=0;k<n;++k){
      g_frameDt=h;g_frameScale=h*50.0F;
      vehSubStepRepeat_=k>0;
      // Presentation remains once per frame, but object/spinner motion
      // invalidates both the vehicle collider list and per-car gather cache.
      vehSubStepMore_=TYRA_VEH_SUBSTEP_REUSE && k+1<n;
      static std::vector<int> restBefore;restBefore.resize(runtimeObjects.size());
      if(n>1)for(size_t i=0;i<runtimeObjects.size();++i)if(runtimeObjects[i].data.physics){
        auto& o=runtimeObjects[i];o.restFrames=UnifiedTimeline::beforeSleep(int(i),o.restFrames,h);restBefore[i]=o.restFrames;
      }
      broadPhaseInvalidate();updateObjectPhysics();++row.physics;
      if(n>1)for(size_t i=0;i<runtimeObjects.size();++i)if(runtimeObjects[i].data.physics)
        UnifiedTimeline::afterSleep(int(i),restBefore[i],runtimeObjects[i].restFrames,h);
      updateSpinners();broadPhaseInvalidate();
      for(VehGatherCache& cache:vehGather_)cache.valid=false;
      if(k>0 && TYRA_VEH_SUBSTEP_REUSE)buildVehicleColliders();
      updateVehicles(vehicleH);++row.vehicles;
      broadPhaseInvalidate();
    }
    if(n>1)for(size_t i=0;i<runtimeObjects.size();++i)if(runtimeObjects[i].data.physics){
      auto& o=runtimeObjects[i];o.velocityX*=float(n);o.velocityY*=float(n);o.velocityZ*=float(n);
      for(float& spin:o.spin)spin*=float(n);
      o.restFrames=UnifiedTimeline::exportSleep(int(i),o.restFrames,frameDt);
      if(i<physSlotOf.size() && physSlotOf[i]>=0){auto& b=physBodies[size_t(physSlotOf[i])];
        for(int a=0;a<3;++a){b.w[a]*=float(n);b.lastSpin[a]=o.spin[a];}
      }
    }
    vehSubStepRepeat_=vehSubStepMore_=false;
    UnifiedTimeline::unitScale=1;g_frameDt=savedDt;g_frameScale=savedScale;
    updateVehicleTutorial(vehicleDt);
  }
  for(const auto& o:runtimeObjects)if(o.active && o.data.physics){
    for(float value:o.data.position)if(!std::isfinite(value))++row.errors;
    if(!std::isfinite(o.velocityX+o.velocityY+o.velocityZ))++row.errors;
    for(float value:o.spin)if(!std::isfinite(value))++row.errors;
    if(Tyra::vu0SatVerify){
      for(float value:o.data.position)UnifiedTimeline::real(row,value);
      for(float value:o.data.rotation)UnifiedTimeline::real(row,value);
      for(float value:o.spin)UnifiedTimeline::real(row,value);
      for(float value:{o.velocityX,o.velocityY,o.velocityZ})UnifiedTimeline::real(row,value);
      UnifiedTimeline::word(row,unsigned(o.restFrames));
    }
  }
  for(int i=0;i<vehicleCount_;++i)if(vehicles_[i].active){
    const auto& v=vehicles_[i];for(float value:v.pos)if(!std::isfinite(value))++row.errors;
    if(!std::isfinite(v.speed+v.lateral+v.yaw))++row.errors;
    if(Tyra::vu0SatVerify){
      for(float value:v.pos)UnifiedTimeline::real(row,value);
      for(float value:{v.speed,v.lateral,v.yaw,v.spin,v.damage})UnifiedTimeline::real(row,value);
      UnifiedTimeline::word(row,unsigned(v.gear));UnifiedTimeline::word(row,unsigned(v.grounded));
    }
  }
  row.ticks=CB::ticks()-begin;
  UnifiedTimeline::rows.push_back(row);
}
