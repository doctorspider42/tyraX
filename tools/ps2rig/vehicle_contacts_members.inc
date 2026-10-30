void TerrainGame::captureVehicleContactStart() {
  VehicleContacts::cars.clear();VehicleContacts::of.assign(runtimeObjects.size(),-1);
  if(!VehicleContacts::enabled)return;
  for(int vi=0;vi<vehicleCount_;++vi){const auto& v=vehicles_[vi];
    if(!v.active || v.def<0 || v.object<0 || size_t(v.object)>=runtimeObjects.size())continue;
    const auto& o=runtimeObjects[v.object];if(!o.active || !physObstacle(o.data))continue;
    VehicleContacts::Car c;c.object=v.object;c.vehicle=vi;
    c.oldOrigin=rbV(o.data.position[0],o.data.position[1],o.data.position[2]);
    const V3 ax=boxRotate({1,0,0},o.data),ay=boxRotate({0,1,0},o.data),az=boxRotate({0,0,1},o.data);
    const float r[9]={ax.x,ay.x,az.x,ax.y,ay.y,az.y,ax.z,ay.z,az.z};
    for(int k=0;k<9;++k)c.oldR[k]=r[k];
    VehicleContacts::of[size_t(c.object)]=int(VehicleContacts::cars.size());VehicleContacts::cars.push_back(c);
  }
}

void TerrainGame::captureVehicleContactPrediction(float dt) {
  VehicleContacts::dt=dt;
  for(auto& c:VehicleContacts::cars){
    auto& v=vehicles_[c.vehicle];const auto& d=runtimeObjects[c.object].data;
    const CollisionBox cb=objectCollisionBox(runtimeObjects[c.object]);
    for(int k=0;k<3;++k){c.localCentre[k]=cb.center[k];c.half[k]=std::max(cb.half[k],1e-4f);}
    const V3 ax=boxRotate({1,0,0},d),ay=boxRotate({0,1,0},d),az=boxRotate({0,0,1},d);
    const float r[9]={ax.x,ay.x,az.x,ax.y,ay.y,az.y,ax.z,ay.z,az.z};for(int k=0;k<9;++k)c.R[k]=r[k];
    const RbV local=rbV(c.localCentre[0],c.localCentre[1],c.localCentre[2]);
    c.oldCentre=rbAdd(c.oldOrigin,rbMat(c.oldR,local));
    c.centre=rbAdd(rbV(d.position[0],d.position[1],d.position[2]),rbMat(c.R,local));
    const float deg=PI/180.f,y=v.yaw*deg,roll=(v.roll+v.leanRoll)*deg;
    const float sy=sinf(y),cy=cosf(y),sr=sinf(roll),cr=cosf(roll);
    const float p=v.pitchVel*deg*dt,rvel=v.rollVel*deg*dt;
    c.angular=rbV(rvel*sy-p*cy*cr,v.spin*deg*dt-p*sr,rvel*cy+p*sy*cr);
    c.originVelocity=rbV((v.speed*sy+v.lateral*cy)*dt,v.velY*dt,(v.speed*cy-v.lateral*sy)*dt);
    c.velocity=rbAdd(c.originVelocity,rbCross(c.angular,rbMat(c.R,local)));
    v.dmgPreV[0]=c.originVelocity.x/dt;v.dmgPreV[1]=c.originVelocity.z/dt;v.dmgHaveN=0;
    c.mass=std::max(VEHICLE_DEFS[v.def].mass,.1f);
    const float hx=c.half[0],hy=c.half[1],hz=c.half[2];
    const float ix=c.mass*(hy*hy+hz*hz)/3.f,iy=c.mass*(hx*hx+hz*hz)/3.f,iz=c.mass*(hx*hx+hy*hy)/3.f;
    c.invYaw=1.f/(ix*c.R[3]*c.R[3]+iy*c.R[4]*c.R[4]+iz*c.R[5]*c.R[5]);
    runtimeObjects[c.object].restFrames=0;
    // Wake swept candidates before their ONE prediction/world-contact pass.
    // The scene broad phase already holds full collider boxes and motion pad.
    Tyra::BroadBox area;
    for(int k=0;k<3;++k){const float a=VehicleContacts::component(c.oldCentre,k),b=VehicleContacts::component(c.centre,k);
      const float radius=sqrtf(hx*hx+hy*hy+hz*hz);
      area.mn[k]=std::min(a,b)-radius;area.mx[k]=std::max(a,b)+radius;
    }
    if(rbDot(rbSub(c.centre,c.oldCentre),rbSub(c.centre,c.oldCentre))>1e-12f || rbDot(c.angular,c.angular)>1e-12f){
      static std::vector<int> near;broadPhaseNear(area,near);
      for(int j:near){auto& o=runtimeObjects[j];if(o.active && o.data.physics && physAsleep(o)){
        o.restFrames=0;++VehicleContacts::current.woken;
      }}
    }
  }
  VehicleContacts::current.cars=unsigned(VehicleContacts::cars.size());
}

void TerrainGame::prepareVehicleContactShape(int object) {
  const int ci=VehicleContacts::index(object);if(ci<0)return;
  const auto& c=VehicleContacts::cars[size_t(ci)];auto& b=physBodies[size_t(physSlotOf[object])];
  b.verts=8;b.planes=6;b.edges=3;b.tumble=true;b.invMass=1.f/c.mass;
  for(int k=0;k<3;++k)b.comL[k]=c.localCentre[k];
  for(int k=0;k<6;++k)b.invI[k]=0;
  b.invI[1]=c.invYaw;
  b.radius=sqrtf(c.half[0]*c.half[0]+c.half[1]*c.half[1]+c.half[2]*c.half[2]);
  for(int corner=0;corner<8;++corner)for(int k=0;k<3;++k)b.lv[corner*3+k]=(corner&(1<<k))?c.half[k]:-c.half[k];
  for(int face=0;face<6;++face){for(int k=0;k<4;++k)b.lp[face*4+k]=0;
    b.lp[face*4+face/2]=(face&1)?-1.f:1.f;b.lp[face*4+3]=c.half[face/2];}
  for(int k=0;k<9;++k)b.le[k]=(k%4==0)?1.f:0.f;
}

void TerrainGame::finishVehicleContact(int object,RbV velocity,RbV angular,RbV vp,RbV wp) {
  const int ci=VehicleContacts::index(object);if(ci<0)return;
  const auto& c=VehicleContacts::cars[size_t(ci)];auto& v=vehicles_[c.vehicle];auto& o=runtimeObjects[object];
  const RbV dv=rbSub(velocity,c.velocity),dw=rbSub(angular,c.angular),move=rbAdd(dv,vp);
  const float dyaw=dw.y+wp.y;
  if(rbDot(move,move)==0 && rbDot(dw,dw)==0 && dyaw==0)return;
  VehicleContacts::current.carCorrection+=sqrtf(rbDot(move,move))+fabsf(dyaw);
  const RbV previousOrigin=rbV(v.pos[0],v.pos[1],v.pos[2]);
  v.yaw+=dyaw*(180.f/PI);v.spin+=dw.y*(180.f/PI)/VehicleContacts::dt;
  vehBodyRotation(v.pitch+v.leanPitch,v.yaw,v.roll+v.leanRoll,o.data.rotation);
  const V3 offset=boxRotate({c.localCentre[0],c.localCentre[1],c.localCentre[2]},o.data);
  const RbV centre=rbAdd(c.centre,move),origin=rbSub(centre,rbV(offset.x,offset.y,offset.z));
  const RbV originVelocity=rbSub(velocity,rbCross(angular,rbV(offset.x,offset.y,offset.z)));
  const float sy=sinf(v.yaw*(PI/180.f)),cy=cosf(v.yaw*(PI/180.f)),invDt=1.f/VehicleContacts::dt;
  v.dmgPreV[0]=c.originVelocity.x*invDt;v.dmgPreV[1]=c.originVelocity.z*invDt;v.dmgHaveN=0;
  v.speed=(originVelocity.x*sy+originVelocity.z*cy)*invDt;
  v.lateral=(originVelocity.x*cy-originVelocity.z*sy)*invDt;v.velY=originVelocity.y*invDt;
  for(int k=0;k<3;++k){v.pos[k]=VehicleContacts::component(origin,k);o.data.position[k]=v.pos[k];}
  if(o.onMatrixPath)updateObjMat(object);else o.dirty=true;v.objMatPending=false;
  if(c.vehicle==vehicleDriver_){
    // The driver's view was predicted once; apply the solved rigid correction.
    const float cs=cosf(dyaw),sn=sinf(dyaw);
    auto correct=[&](Vec4& point){const float x=point.x-previousOrigin.x,z=point.z-previousOrigin.z;
      point.x=origin.x+cs*x+sn*z;point.y+=origin.y-previousOrigin.y;point.z=origin.z-sn*x+cs*z;};
    correct(cameraPosition);correct(cameraLookAt);
    players[0].x=cameraPosition.x;players[0].y=cameraPosition.y;players[0].z=cameraPosition.z;players[0].yaw=v.yaw*(PI/180.f);
    engine->renderer.core.renderer3D.update(CameraInfo3D(&cameraPosition,&cameraLookAt,&cameraUp));
  }
}

void TerrainGame::stepVehicleBodyContacts(float frameDt) {
  if(!CB::ready || CB::finished)return;
  VehicleContacts::enabled=!CB::baseline;VehicleContacts::begin();const unsigned start=CB::ticks();
  for(int vi=0;vi<vehicleCount_;++vi)if(vehicles_[vi].active)++VehicleContacts::current.cars;
  if(VehicleContacts::enabled){
    captureVehicleContactStart();stepVehicles(g_frameScale*(1.f/50.f));
    broadPhaseInvalidate();captureVehicleContactPrediction(frameDt);
    updateObjectPhysics();updateSpinners();broadPhaseInvalidate();
    // Body collisions use a separate pre-impact velocity, so wall/car damage
    // already processed during prediction is not charged a second time.
    updateVehicleDamage(0.f);
  }else{
    updateObjectPhysics();updateSpinners();broadPhaseInvalidate();stepVehicles(g_frameScale*(1.f/50.f));
  }
  for(const auto& o:runtimeObjects)if(o.active && (o.data.physics || VehicleContacts::isCar(int(&o-runtimeObjects.data())))){
    for(float value:o.data.position)if(!std::isfinite(value))++VehicleContacts::current.errors;
    if(!std::isfinite(o.velocityX+o.velocityY+o.velocityZ))++VehicleContacts::current.errors;
  }
  if(Tyra::vu0SatVerify){
    for(const auto& o:runtimeObjects)if(o.active && o.data.physics){
      for(float value:o.data.position)VehicleContacts::real(value);
      for(float value:o.data.rotation)VehicleContacts::real(value);
      for(float value:o.spin)VehicleContacts::real(value);
      for(float value:{o.velocityX,o.velocityY,o.velocityZ})VehicleContacts::real(value);
      VehicleContacts::word(unsigned(o.restFrames));
    }
    for(int vi=0;vi<vehicleCount_;++vi)if(vehicles_[vi].active){const auto& v=vehicles_[vi];
      for(float value:v.pos)VehicleContacts::real(value);
      for(float value:{v.speed,v.lateral,v.velY,v.yaw,v.spin,v.pitch,v.roll,v.damage})VehicleContacts::real(value);
    }
  }
  VehicleContacts::current.combinedTicks=CB::ticks()-start;
  VehicleContacts::rows.push_back(VehicleContacts::current);
}
