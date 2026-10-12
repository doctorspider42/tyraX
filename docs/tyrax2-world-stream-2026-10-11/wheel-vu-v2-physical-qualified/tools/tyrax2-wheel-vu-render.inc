    auto& localWheel=WheelVu::source(v.fastWheels && fastPart?fm:wm,geo,nv,run);
    if(WheelVu::active){
      float bodyRot[3];
      vehBodyRotation(v.pitch+v.leanPitch,v.yaw,v.roll+v.leanRoll,bodyRot);
      const RotTrig br=rotTrigOf(bodyRot);
      const V3 up=rotatedBy({0,1,0},br);
      const float sp=v.wheelSpin*kDeg,cp=cosf(sp),sn=sinf(sp);
      const float hx=0.5F*s.track*SC,hz=0.5F*s.wheelBase*SC;
      const float lx[4]={-hx,hx,-hx,hx},lz[4]={hz,hz,-hz,-hz};
      V3 bx[2],by[2],bz[2];
      for(int pair=0;pair<2;++pair){
        const float st=(pair==0?v.steerAngle:0)*kDeg,cs=cosf(st),ss=sinf(st);
        bx[pair]=rotatedBy({cs*SC,0,-ss*SC},br);
        by[pair]=rotatedBy({sn*ss*SC,cp*SC,sn*cs*SC},br);
        bz[pair]=rotatedBy({cp*ss*SC,-sn*SC,cp*cs*SC},br);
      }
      for(int w=0;w<4;++w){
        const V3 hard=rotatedBy({lx[w],0,lz[w]},br);
        const float targetY=v.wheelY[w]+s.wheelRadius*SC;
        float travel=up.y>0.2F?(targetY-(v.pos[1]+hard.y))/up.y:0;
        const float lo=-s.suspensionTravel*SC*0.45F;
        const float a=s.suspensionTravel*SC*0.10F,b=s.wheelRadius*SC*0.06F;
        const float hi=a<b?a:b;
        if(travel<lo)travel=lo;
        if(travel>hi)travel=hi;
        const int pair=w<2?0:1;
        Tyra::M4x4 matrix;
        const float values[16]={bx[pair].x,bx[pair].y,bx[pair].z,0,
          by[pair].x,by[pair].y,by[pair].z,0,bz[pair].x,bz[pair].y,bz[pair].z,0,
          v.pos[0]+hard.x+up.x*travel,v.pos[1]+hard.y+up.y*travel,
          v.pos[2]+hard.z+up.z*travel,1};
        memcpy(matrix.data,values,sizeof(values));
        WheelVu::submit(stapip,localWheel,matrix,*batchInfoBag,
          const_cast<Tyra::Texture*>(part.texture),receiverClasses==2 && receiverClass==0?
            Tyra::StaPipLightReception::Suppress:Tyra::StaPipLightReception::FromInfo);
      }
      ++WheelVu::cars;continue;
    }
