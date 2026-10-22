from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001'); parent=lab/'tyrax2-closeout-motion-v1/quiet-v4'; out=lab/'tyrax2-city-day-drive-v1'
assert not out.exists()
def ignore(d,n): return [x for x in n if x in {'obj','__pycache__','.git','logs','screenshots','preview','authoring'} or x.endswith(('.elf','.sym','.log')) or x in {'log.txt','closeout-samples.txt','ps2link.run'}]
out.mkdir();shutil.copytree(parent/'tyra',out/'tyra',ignore=ignore);shutil.copytree(parent/'vehicle/game',out/'vehicle/game',ignore=ignore)
game=out/'vehicle/game'
def edit(p,a,b):
 s=p.read_text();assert s.count(a)==1,(p,a);p.write_bytes(s.replace(a,b).encode())
edit(out/'tyra/engine/inc/debug/closeout.hpp','restricted=kind==3||kind==2||(kind==0&&middle(p));','restricted=false; /* PRIVATE fixed public All receivers for daylight diagnosis */')
edit(game/'inc/scene_data.hpp','SAVE_VALUE_DEFAULTS[SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1] = {1.0F};','SAVE_VALUE_DEFAULTS[SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1] = {0.0F};')
edit(game/'inc/motion_route.hpp','if (route == 1) {','if (route == 1 && frame < 1800U) {')
p=game/'src/terrain_game.cpp';s=p.read_text();s=s.replace('g_activeScene != 0 || !night || !daynight::g_paused || daynight::g_hour != 0.0F || !driverGood || !Closeout::restricted','g_activeScene != 0 || night || !daynight::g_paused || daynight::g_hour != 12.0F || !driverGood || Closeout::restricted').replace('players=1 oneArm=1 route=1','players=0 oneArm=1 route=1').replace('unsigned(daynight::g_hour==0.0F)','unsigned(daynight::g_hour==12.0F)')
marker='  // PRIVATE cold actual motion endpoints; no clock reads or hot output.';assert s.count(marker)==1
block='''  // PRIVATE daylight route/view diagnosis. Sparse pose stores, no new clocks/waits.
  struct DayPose { float v[12]; };
  static DayPose poses[5]{};
  static unsigned captured=0, selected=0;
  static bool parked=false;
  const unsigned dayFrame=NightSampler::frame;
  if(dayFrame>=832 && dayFrame<=1088 && (dayFrame-832)%64==0) {
    const unsigned i=(dayFrame-832)/64;
    if(vehicleDriver_<0 || vehicleDriver_>=VEHICLE_COUNT) NightSampler::valid=false;
    else {
      const auto& v=vehicles_[vehicleDriver_];
      poses[i]={{v.pos[0],v.pos[1],v.pos[2],v.yaw,v.pitch,v.roll,
        cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z}};
      captured|=1u<<i;
    }
  }
  if(dayFrame==1155) {
    if(captured!=31) NightSampler::valid=false;
    unsigned worst=0;
    for(unsigned i=0;i<5;++i) {
      const auto& c=NightSampler::chunks[0][i];
      if(c.loops!=64 || c.pacing>c.elapsed) NightSampler::valid=false;
      const unsigned work=c.elapsed-c.pacing;
      if(i==0 || work>worst){worst=work;selected=i;}
      unsigned bits[12];for(unsigned j=0;j<12;++j)memcpy(&bits[j],&poses[i].v[j],4);
      printf("LOG: DAYPOSE chunk=%u sampleFrame=%u workTicks=%u carX=%08x carY=%08x carZ=%08x yaw=%08x pitch=%08x roll=%08x camX=%08x camY=%08x camZ=%08x lookX=%08x lookY=%08x lookZ=%08x\\n",i,832+i*64,work,bits[0],bits[1],bits[2],bits[3],bits[4],bits[5],bits[6],bits[7],bits[8],bits[9],bits[10],bits[11]);
    }
    printf("LOG: DAYSELECT chunk=%u captured=%u valid=%u\\n",selected,captured,unsigned(NightSampler::valid));fflush(stdout);
  }
  if(dayFrame==1700) {
    if(captured!=31 || vehicleDriver_<0 || vehicleDriver_>=VEHICLE_COUNT) NightSampler::valid=false;
    else {
      auto& v=vehicles_[vehicleDriver_];const auto& p=poses[selected].v;
      for(unsigned j=0;j<3;++j)v.pos[j]=p[j];
      v.yaw=p[3];v.pitch=p[4];v.roll=p[5];
      v.speed=0;v.lateral=0;v.velY=0;v.spin=0;v.steerAngle=0;
      parked=true;
    }
    printf("LOG: DAYPARK frame=%u selected=%u parked=%u valid=%u\\n",dayFrame,selected,unsigned(parked),unsigned(NightSampler::valid));fflush(stdout);
  }
  if(parked) {
    const auto& p=poses[selected].v;
    cameraPosition.set(p[6],p[7],p[8]);cameraLookAt.set(p[9],p[10],p[11]);
  }
'''
s=s.replace(marker,block+marker);p.write_bytes(s.encode())
(game/'bin/closeout.cfg').write_bytes(b'0 3\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files={}
for base,prefix in ((game/'src','game/src'),(game/'inc','game/inc'),(out/'tyra/engine/src','tyra/engine/src'),(out/'tyra/engine/inc','tyra/engine/inc')):
 for f in base.rglob('*'):
  if f.is_file():files[prefix+'/'+f.relative_to(base).as_posix()]=sha(f)
for name in ('Makefile',next(game.glob('*.tyra')).name,'bin/closeout.cfg'):files['game/'+name]=sha(game/name)
for name in ('tyra/Makefile.base','tyra/engine/Makefile'):files[name]=sha(out/name)
(out/'vehicle/source-review.json').write_text(json.dumps(dict(status='PRIVATE_DAY_DRIVE_SOURCE_FROZEN',files=files,route=1,day=True,allReceivers=True,commonScaffoldingCostUnpriced=True,parentProofSha256=sha(parent/'vehicle/native-proof.json')),indent=2)+'\n')
(out/'README.md').write_text('Private full-city daytime drive diagnosis. Phase0 uses the inherited five64-loop drive windows. Five midpoint car/view poses are stored without clocks; after all windows complete, the largest elapsed-minus-pacing chunk selects its representative midpoint. At frame1700, the car is relocated once, velocities cleared once, then ordinary physics/logical updates continue with neutral input. Camera is held at the selected midpoint for phases1/2, after900 warm-up loops. This is a representative-view diagnostic, NOT an exact moving-versus-stationary causal subtraction: the moving chunk has changing view, while parked suspension/state can settle. Original ordinary sampler, pipeline and waits remain unchanged; new sparse pose storage/controller/selection footprint is common but unpriced. All receivers; fixed noon. Quiet private healthy telemetry suppression retained. No production map changes.\n')
print(out)
