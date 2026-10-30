bool TerrainGame::benchTrafficBegin() {
  using namespace TrafficBench;
  if(finished){fflush(stdout);SleepThread();return false;}
  U now=ticks();
  if(ready){
    if(previous){current.period=U(now-frameStart);rows.push_back(current);}
    if(float(U(now-phaseStart))/Hz>=6.f){
      printf("TRAFFICBENCH RAM phase=%d free_mb=%.4f\n",phase,engine->info.getAvailableRAM());
      flush();ready=false;previous=false;++phase;
      if(phase>=10){finished=true;printf("TRAFFICBENCH DONE phases=%d start_phase=%d\n",10-firstPhase,firstPhase);fflush(stdout);return false;}
      loadScene(0);g_frameClockLast=0;return false;
    }
  }else{
    if(vehicleCount_!=13 || vehicleDriver_<0 || tfScene_<0)return true;
    benchTrafficSetup();if(finished)return false;
    now=ticks();phaseStart=now;ready=true;frame=0;rows.reserve(1024);
  }
  resetRow();current.phase=unsigned(phase);current.frame=unsigned(frame++);
  current.elapsed=float(U(now-phaseStart))/Hz;frameStart=now;previous=false;
  return true;
}

void TerrainGame::benchTrafficSetup() {
  using namespace TrafficBench;
  wantedHQ=std::min(populations[phase],5);wantedCiv=std::max(populations[phase]-5,0);
  if(tfVehicle_.size()!=8){finished=true;printf("TRAFFICBENCH ERROR traffic slots\n");return;}
  int hq[5]={vehicleDriver_,-1,-1,-1,-1},count=1;
  for(int vi=0;vi<vehicleCount_;++vi)if(vi!=vehicleDriver_ && tfCarOf_[size_t(vi)]<0){
    if(count>=5){finished=true;printf("TRAFFICBENCH ERROR HQ slots\n");return;}hq[count++]=vi;
  }
  if(count!=5){finished=true;printf("TRAFFICBENCH ERROR HQ count\n");return;}
  tf_.reset(8);tf_.clock=0;tfRng_=12345;tfLenT_=0;
  const float x[5]={0,-4.875f,-1.625f,-4.875f,-1.625f};
  const float z[5]={-74.3035f,-64,-64,-54,-54};
  for(int i=0;i<5;++i){auto& v=vehicles_[hq[i]];auto& o=runtimeObjects[v.object];
    v.active=i<wantedHQ;o.active=o.visible=v.active;
    v.pos[0]=x[i];v.pos[1]=.5f;v.pos[2]=z[i];v.yaw=0;
    v.speed=v.lateral=v.velY=v.spin=v.pitch=v.roll=v.pitchVel=v.rollVel=0;
    v.wpCur=0;v.aiPrevX=x[i];v.aiPrevZ=z[i];
  }
  for(int ci=0;ci<8;++ci){int vi=tfVehicle_[size_t(ci)];auto& v=vehicles_[vi];auto& o=runtimeObjects[v.object];
    v.active=0;o.active=o.visible=false;
    if(ci>=wantedCiv)continue;
    const float tx=(ci&1)?4.875f:1.625f,tz=-68.f+7.f*float(ci/2);
    float best=1e30f,arc=0,bestXZ=1e30f,bestY=0;int pick=-1;
    // Initial placement searches the entire actual lane polyline. The normal
    // per-frame local projection window is inappropriate for initial seeds.
    for(int si=0;si<int(tf_.g.segs.size());++si){const auto& s=tf_.g.segs[size_t(si)];
      if(s.kind!=0 && !(s.kind==1 && s.turn==0))continue;
      for(int k=s.first;k+1<s.first+s.count;++k){const float* a=&tf_.g.pts[size_t(k)*3];const float* b=a+3;
        const float dx=b[0]-a[0],dz=b[2]-a[2],len2=dx*dx+dz*dz;
        if(!(len2>1e-8f) || dz<.8f*sqrtf(len2))continue;
        const float t=std::max(0.f,std::min(1.f,((tx-a[0])*dx+(tz-a[2])*dz)/len2));
        const float px=a[0]+dx*t,pz=a[2]+dz*t,py=a[1]+(b[1]-a[1])*t;
        if(fabsf(py)>1.f)continue; // this fixture seeds the ground-level boulevard
        const float xz=(px-tx)*(px-tx)+(pz-tz)*(pz-tz),e=xz+.01f*py*py;
        if(e<best){best=e;bestXZ=xz;bestY=py;pick=si;arc=tf_.g.arc[size_t(k)]+(tf_.g.arc[size_t(k+1)]-tf_.g.arc[size_t(k)])*t;}
      }
    }
    if(pick<0 || bestXZ>.25f){finished=true;printf("TRAFFICBENCH ERROR seed ci=%d xz_error2=%g lane_y=%g\n",ci,bestXZ,bestY);return;}
    printf("TRAFFICBENCH SEED ci=%d segment=%d arc=%g xz_error2=%g lane_y=%g\n",ci,pick,arc,bestXZ,bestY);
    o.active=true;trafficPlace(ci,pick,arc);v.speed=0;tf_.cars[size_t(ci)].speed=0;
  }
  // Publish initial poses once. Later movement is entirely normal dynamics.
  for(int vi=0;vi<vehicleCount_;++vi){auto& v=vehicles_[vi];auto& o=runtimeObjects[v.object];
    for(int k=0;k<3;++k)o.data.position[k]=v.pos[k];
    vehBodyRotation(v.pitch+v.leanPitch,v.yaw,v.roll+v.leanRoll,o.data.rotation);
    if(o.onMatrixPath)updateObjMat(v.object);else o.dirty=true;v.objMatPending=false;
  }
  vehColGen_=vehColSetGen_=~0u;vehColFresh_=false;broadPhaseInvalidate();
  objectSlot.assign(runtimeObjects.size(),-1);
  for(int vi=0;vi<vehicleCount_;++vi)objectSlot[size_t(vehicles_[vi].object)]=vi;
  // Every initial pair is tested against its resolved full box, not origins.
  struct Shape {Tyra::CollisionV3 p[8],n[6],e[3];};Shape shape[13];bool active[13]{};
  for(int vi=0;vi<vehicleCount_;++vi){const auto& v=vehicles_[vi];if(!v.active)continue;active[vi]=true;
    const auto& o=runtimeObjects[v.object];const auto box=objectCollisionBox(o);auto& s=shape[vi];
    for(int k=0;k<3;++k){V3 axis=boxRotate(k==0?V3{1,0,0}:k==1?V3{0,1,0}:V3{0,0,1},o.data);
      s.e[k]={axis.x,axis.y,axis.z};s.n[2*k]=s.e[k];s.n[2*k+1]={-axis.x,-axis.y,-axis.z};}
    for(int k=0;k<8;++k){const V3 p=boxRotate({box.center[0]+((k&1)?box.half[0]:-box.half[0]),box.center[1]+((k&2)?box.half[1]:-box.half[1]),box.center[2]+((k&4)?box.half[2]:-box.half[2])},o.data);
      s.p[k]={p.x+v.pos[0],p.y+v.pos[1],p.z+v.pos[2]};}
  }
  unsigned overlaps=0;
  for(int a=0;a<13;++a)if(active[a])for(int b=a+1;b<13;++b)if(active[b]){
    Tyra::ConvexContact c;const auto& A=shape[a];const auto& B=shape[b];
    if(Tyra::convexMotionContact({A.p,8,A.n,6,A.e,3},{B.p,8,B.n,6,B.e,3},{0,0,0},{0,0,0},c))++overlaps;
  }
  printf("TRAFFICBENCH START phase=%d hq=%d civ=%d initial_box_overlaps=%u\n",phase,wantedHQ,wantedCiv,overlaps);fflush(stdout);
  if(overlaps){finished=true;printf("TRAFFICBENCH ERROR initial overlap\n");}
}

void TerrainGame::benchTrafficEnd() {
  using namespace TrafficBench;if(!ready || finished)return;
  current.body=U(ticks()-frameStart);current.dt=g_frameDt;
  float fx=cameraLookAt.x-cameraPosition.x,fz=cameraLookAt.z-cameraPosition.z;
  for(int vi=0;vi<vehicleCount_;++vi){const auto& v=vehicles_[vi];if(!v.active)continue;
    const bool civilian=tfCarOf_[size_t(vi)]>=0;
    const float dx=v.pos[0]-cameraPosition.x,dz=v.pos[2]-cameraPosition.z,dist=sqrtf(dx*dx+dz*dz);
    const bool near=dist<48.f,front=dx*fx+dz*fz>0,lod0=vehicleLod(vi)==0;
    if(civilian){++current.civ;current.civNear+=near;current.civFront+=front;current.civLod0+=lod0;current.civSubmitted+=submittedSeen[vi];}
    else{++current.hq;current.hqNear+=near;current.hqFront+=front;current.hqLod0+=lod0;current.hqSubmitted+=submittedSeen[vi];}
    current.maxDistance=std::max(current.maxDistance,dist);
    if(!std::isfinite(v.pos[0]+v.pos[1]+v.pos[2]+v.speed+v.yaw+v.damage))++current.c[Errors];
  }
  const auto& driver=vehicles_[vehicleDriver_];current.z=driver.pos[2];current.speed=driver.speed;
  previous=true;
}
