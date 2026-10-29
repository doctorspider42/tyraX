"""Same-ELF collision stress fixture. See tools/ps2rig/README.md.

The baseline reuses exact main physics/broad-phase code with the candidate's
shared data layout and renderer. This is an algorithm A/B, not two stock builds.
Only box-mode scenery is covered; reject mesh-mode scenes for this comparison.
"""
from pathlib import Path
import subprocess,re,shutil,json
import argparse
parser=argparse.ArgumentParser(description="Prepare/instrument a same-ELF Motor District collision stress test.")
parser.add_argument('command',choices=['prepare','instrument'])
parser.add_argument('--repo',default=str(Path(__file__).resolve().parents[2]))
parser.add_argument('--scratch',required=True,help="Primary checkout's ignored .tmp/tests/<task> root")
parser.add_argument('--baseline',default='e4acb7379',help='Baseline commit for exact physics and broad-phase source')
parser.add_argument('--uncapped-baseline',action='store_true',help='Remove the reference solver cap for an equal-completeness comparison')
parser.add_argument('--solver-comparison',action='store_true',help='42 phases: prior solver, prepared scalar, prepared VU0; reverse order on repeat')
parser.add_argument('--temporal-comparison',action='store_true',help='42 phases: reference, geometry cache, cache + warm starting + islands')
parser.add_argument('--temporal-arms',type=int,choices=[2,3,4],default=4,help='Four arms separate warm starting from islands; three replays the initial prototype')
parser.add_argument('--settle-check',action='store_true',help='Six stationary 64-box pile phases: settle, known base impulse, recover')
parser.add_argument('--micro-comparison',action='store_true',help='28 phases: current scalar physics versus asynchronous VU0 SAT/CCD')
args=parser.parse_args()
if args.micro_comparison:args.temporal_comparison=True;args.temporal_arms=2
if args.settle_check:args.temporal_comparison=True
if args.temporal_comparison:args.solver_comparison=True
repo=Path(args.repo).resolve();root=Path(args.scratch).resolve();base=args.baseline
common=Path(subprocess.check_output(['git','-C',str(repo),'rev-parse','--path-format=absolute','--git-common-dir'],text=True).strip())
scratch_root=common.parent/'.tmp'
if not root.is_relative_to(scratch_root.resolve()) or root==scratch_root.resolve():
 raise SystemExit('Scratch must be a task directory under the primary checkout .tmp/')
proj=root/'district';eng=root/'engine/engine';rig=Path(__file__).resolve().parent
if args.command=='prepare':
 if proj.exists() or eng.parent.exists():raise SystemExit('Use a fresh scratch root: fixture/engine already exists')
 root.mkdir(parents=True,exist_ok=True)
 shutil.copytree(repo/'examples/vehicle-playground',proj,ignore=shutil.ignore_patterns('bin','obj','*.history'))
 mp=next(proj.glob('*.tyra'));d=json.loads(mp.read_text(encoding='utf8'));d['scenes']=d['scenes'][:1];st=d['settings']
 for objfile in (proj/'objects').glob('*.json'):
  if json.loads(objfile.read_text(encoding='utf8')).get('collision')=='mesh':
   raise SystemExit('This algorithm comparison requires a fixture without mesh-mode colliders')
 for k in ['liveDebug','liveLink','liveLogic','timeMachine','remotePad','inputRecorder','keyboardMouse','keyboardMousePs2Link','showFps','showMemory','showProfiler']:st[k]=False
 st.update(buildProfile='release',disableVsync=False,displayMode='progressive',videoSystem='ntsc')
 for i in range(128):
  oid=f'collisionbench{i:04d}';obj=dict(id=oid,name=f'CB body {i:03d}',type='box',position=[0,.5,-48],rotation=[0,0,0],scale=[.9,.9,.9],color=[.8,.35,.1],physics=True,physMass=.6,physBounce=.1,physFriction=.6,physTumble=True,physSleep=1.5,shadowMode=0)
  (proj/'objects'/f'{oid}.json').write_text(json.dumps(obj),encoding='utf8');d['scenes'][0]['objects'].append(oid)
 mp.write_text(json.dumps(d,indent=2),encoding='utf8')
 eng.parent.mkdir()
 shutil.copytree(repo/'vendor/tyra/engine',eng,ignore=shutil.ignore_patterns('obj','bin'))
 for name in ['Makefile.base','LICENSE']:shutil.copyfile(repo/'vendor/tyra'/name,eng.parent/name)
 shutil.copytree(repo/'vendor/tyra/audsrv',eng.parent/'audsrv',ignore=shutil.ignore_patterns('.work'))
 print('Prepared',proj,'- run the editor full --build, then instrument; do not refresh generated sources after instrumentation.')
 raise SystemExit(0)
if not (proj/'src/gen/game_physics.gen.cpp').is_file():raise SystemExit('Generate/build the prepared project before instrumenting')
if 'updateObjectPhysicsCandidate' in (proj/'inc/terrain_game.hpp').read_text(encoding='utf8'):raise SystemExit('Fixture is already instrumented')

# Strip BOMs and preserve LF for every injected engine source.
(eng/'inc/debug/collision_bench.hpp').write_bytes((('#define TYRA_TEMPORAL_SETTLE 1\n' if args.settle_check else '')+(f'#define TYRA_TEMPORAL_ARMS {args.temporal_arms}\n' if args.temporal_comparison else '')+('#define TYRA_SOLVER_COMPARISON 1\n#define TYRA_TEMPORAL_COMPARISON 1\n' if args.temporal_comparison else '#define TYRA_SOLVER_COMPARISON 1\n' if args.solver_comparison else '')+(rig/'collision_bench.hpp').read_text(encoding='utf-8-sig')).encode())
for rel in ['inc/physics/broad_phase.hpp','src/physics/broad_phase.cpp']:
 text=subprocess.check_output(['git','-C',str(repo),'show',f'{base}:vendor/tyra/engine/{rel}']).decode('utf8')
 text=text.replace('namespace Tyra {','namespace BaselineTyra {').replace('namespace Tyra','namespace BaselineTyra')
 if rel.endswith('.hpp'):
  a=text.index('struct BroadBox {');b=text.index('\n};',a)+3;text=text[:a]+'using BroadBox = Tyra::BroadBox;'+text[b:]
  text=text.replace('#include <vector>','#include <vector>\n#include "physics/broad_phase.hpp"')
  target=eng/'inc/physics/baseline_broad_phase.hpp'
 else:
  text=text.replace('"physics/broad_phase.hpp"','"physics/baseline_broad_phase.hpp"');target=eng/'src/physics/baseline_broad_phase.cpp'
 target.write_bytes(text.encode())
old=subprocess.check_output(['git','-C',str(repo),'show',f'{base}:src/game_templates.inc']).decode('utf8')
def extract(text,name):
 match=re.search(r'(?m)^(?:void|bool|float) TerrainGame::'+name+r'\(',text);assert match,name
 start=match.start();brace=text.index('{',match.end());depth=0;state='code';i=brace
 while i<len(text):
  c=text[i];n=text[i+1:i+2]
  if state=='line':
   if c=='\n':state='code'
  elif state=='comment':
   if c=='*' and n=='/':state='code';i+=1
  elif state in ['"',"'"]:
   if c=='\\':i+=1
   elif c==state:state='code'
  else:
   if c=='/' and n=='/':state='line';i+=1
   elif c=='/' and n=='*':state='comment';i+=1
   elif c in ['"',"'"]:state=c
   elif c=='{':depth+=1
   elif c=='}':
    depth-=1
    if depth==0:return text[start:i+1]
  i+=1
 raise RuntimeError(name)
# Exact main physics + refresh/query functions, sharing the candidate's data
# layout and renderer. There are no mesh-mode colliders in this fixture.
names=['updateObjectPhysics','broadPhaseRefresh','broadPhaseNear','broadPhaseAlong']
files=[proj/'src/gen'/n for n in ['game_physics.gen.cpp','game_scene.gen.cpp','game_collision.gen.cpp','game_vehicles.gen.cpp','game_lighting.gen.cpp']]
chunks=[]
for name in names:
 chunk=extract(old,name).replace('TerrainGame::'+name+'(', 'TerrainGame::'+name+'Baseline(')
 if args.uncapped_baseline:
  chunk=chunk.replace('      if ((int)contacts.size() >= PHYS_MAX_CONTACTS) return;','')
 if name!='updateObjectPhysics':chunk=re.sub(r'\bbroadPhase\b','baselineBroadPhase',chunk).replace('Tyra::BroadPhase','BaselineTyra::BroadPhase')
 chunks.append(chunk)
 for p in files:
  text=p.read_text(encoding='utf8')
  if 'TerrainGame::'+name+'(' in text:
   text=text.replace('TerrainGame::'+name+'(', 'TerrainGame::'+name+'Candidate(',1);p.write_text(text,encoding='utf8',newline='\n');break
 else:raise RuntimeError('definition missing '+name)
header=proj/'inc/terrain_game.hpp';s=header.read_text(encoding='utf8');s=s.replace('#include <tyra>','#include <tyra>\n#include "physics/baseline_broad_phase.hpp"\n#include "debug/collision_bench.hpp"')
s=s.replace(' private:\n',''' private:
  mutable BaselineTyra::BroadPhase baselineBroadPhase;
  void benchTick();
  void updateObjectPhysicsBaseline(); void updateObjectPhysicsCandidate();
  void broadPhaseRefreshBaseline() const; void broadPhaseRefreshCandidate() const;
  void broadPhaseNearBaseline(const Tyra::BroadBox&,std::vector<int>&) const;
  void broadPhaseNearCandidate(const Tyra::BroadBox&,std::vector<int>&) const;
  void broadPhaseAlongBaseline(const float*,const float*,float,std::vector<int>&) const;
  void broadPhaseAlongCandidate(const float*,const float*,float,std::vector<int>&) const;
''',1);header.write_text(s,encoding='utf8',newline='\n')
wrappers='''
void TerrainGame::updateObjectPhysics(){CB::PhysicsScope scope;if(CB::baseline)updateObjectPhysicsBaseline();else updateObjectPhysicsCandidate();}
void TerrainGame::broadPhaseRefresh() const {if(CB::baseline)broadPhaseRefreshBaseline();else broadPhaseRefreshCandidate();}
void TerrainGame::broadPhaseNear(const Tyra::BroadBox& b,std::vector<int>& out) const {if(CB::baseline)broadPhaseNearBaseline(b,out);else broadPhaseNearCandidate(b,out);}
void TerrainGame::broadPhaseAlong(const float*a,const float*b,float r,std::vector<int>& out) const {if(CB::baseline)broadPhaseAlongBaseline(a,b,r,out);else broadPhaseAlongCandidate(a,b,r,out);}
'''
setup='''
void TerrainGame::benchTick(){
 if(CB::finished)return;
#if TYRA_SOLVER_COMPARISON
 static bool checked=false;if(!checked){checked=true;if(!TyraSolverProbe::run()){CB::finished=true;return;}}
#endif
 if(CB::ready && CB::frame>=CB::total){CB::flush();++CB::phase;CB::ready=false;
  if(CB::phase>=CB::phases){CB::finished=true;printf("CB DONE phases=%d\\n",CB::phase);fflush(stdout);return;}
  CB::configure();loadScene(0);CB::setup=true;
 }
 if(TYRA_TEMPORAL_SETTLE && CB::ready && CB::frame==CB::warm+CB::park){
  const int index=CB_FIRST_BODY;const auto& o=runtimeObjects[index];
  physPushAt(index,o.data.position[0],o.data.position[1],o.data.position[2],.12F,0,0);
 }
 if(CB::setup && !runtimeObjects.empty()){
  CB::configure();int found=0;
  for(int i=0;i<(int)runtimeObjects.size();++i){auto&o=runtimeObjects[i];
   int index=i-135;if(index<0 || index>=128)continue;
  bool enabled=index<CB::added;o.active=enabled;o.visible=enabled;
   float x,z,y;if(CB::layout){x=-1.5F+float(index%4);z=-48.0F+float((index/4)%4);y=.45F+float(index/16)*.92F;}
   else{x=-7.0F+float(index%8)*2.0F;z=-50.0F+float(index/8)*7.0F;y=.45F;}
   o.data.position[0]=x;o.data.position[1]=terrainHeightAt(x,z)+y;o.data.position[2]=z;
   o.data.rotation[0]=o.data.rotation[1]=o.data.rotation[2]=0;
   o.velocityX=o.velocityY=o.velocityZ=0;o.restFrames=0;o.dirty=true;++found;
  }
  if(found!=128){printf("CB ERROR body_count=%d\\n",found);fflush(stdout);CB::finished=true;return;}
  physBodies.clear();physSlotOf.clear();physBodiesScene=-1;bpGen=~0u;bpFrame=~0u;broadPhase.clear();baselineBroadPhase.clear();
  g_frameClockLast=0;CB::frame=CB::phase==0?-300:0;CB::ready=true;CB::setup=false;
  printf("CB START phase=%d baseline=%d added=%d layout=%d rep=%d objects=%u\\n",CB::phase,CB::baseline,CB::added,CB::layout,CB::repeat,(unsigned)runtimeObjects.size());fflush(stdout);
 }
}
'''
rows=re.findall(r'^    \{.*?\},  // (.*)$',(proj/'src/gen/scene_objects.gen.cpp').read_text(encoding='utf8'),re.M)
indices=[i for i,name in enumerate(rows)if name.startswith('CB body ')]
assert len(indices)==128 and indices==list(range(indices[0],indices[0]+128)), 'Unexpected generated body ordering'
setup=setup.replace('int index=i-135',f'int index=i-{indices[0]}')
setup=setup.replace('CB_FIRST_BODY',str(indices[0]))
if args.solver_comparison:
 (eng/'inc/debug/contact_solver_probe.hpp').write_bytes((rig/'contact_solver_probe.hpp').read_text(encoding='utf-8-sig').encode())
 extra_include='#include "debug/contact_solver_probe.hpp"\n'
else:extra_include=''
extra=proj/'src/gen/collision_baseline.gen.cpp';extra.write_text('#include "game_runtime.gen.hpp"\n'+extra_include+'#include <stdlib.h>\nnamespace Vehicle_playground {\n'+ '\n\n'.join(chunks)+wrappers+setup+'\n}\n',encoding='utf8',newline='\n')
# Instrument both solver bodies identically; report truncation rather than
# mistaking bounded work for correct collision at arbitrary object counts.
for p in [proj/'src/gen/game_physics.gen.cpp',extra]:
 s=p.read_text(encoding='utf8')
 s=s.replace('  const int awakeCount = (int)steps.size();','  const int awakeCount = (int)steps.size(); CB::awake += awakeCount;')
 s=s.replace('    const int m = rbReduce(cs, n, red);','    const int m = rbReduce(cs, n, red); CB::generated += m;')
 s=s.replace('if ((int)contacts.size() >= PHYS_MAX_CONTACTS) return;','if ((int)contacts.size() >= PHYS_MAX_CONTACTS) {CB::drops += m-k;return;}')
 s=s.replace('  const int nc = (int)contacts.size();','  const int nc = (int)contacts.size(); CB::contacts += nc;')
 if args.solver_comparison and p.name=='game_physics.gen.cpp':
  assert s.count('TYRA_PHYSICS_SOLVER_VU0 != 0')==1
  s=s.replace('TYRA_PHYSICS_SOLVER_VU0 != 0','CB::solverArm == 2')
  s=('#define TYRA_PHYSICS_PREPARED 0\n' if args.temporal_comparison else '#define TYRA_PHYSICS_PREPARED 1\n')+s
  if args.temporal_comparison:
   if args.micro_comparison:
    s='#define TYRA_PHYSICS_MICRO 1\n'+s
    s=s.replace('TYRA_PHYSICS_MICRO != 0','CB::solverArm == 1')
   options=[('COHERENCE','true'),('WARM_START','false'),('ISLANDS','false')] if args.temporal_arms==2 else [('COHERENCE','CB::solverArm >= 1'),('WARM_START','CB::solverArm >= 2'),('ISLANDS',f'CB::solverArm == {args.temporal_arms-1}')]
   for flag,value in options:
    token='TYRA_PHYSICS_'+flag+' != 0';assert s.count(token)==1;s=s.replace(token,value)
   assert s.index('  // --- 4.') < s.index('  // Independent dynamic components:')
 if args.solver_comparison:
  marker='  // --- 4.';pos=s.index(marker,s.index('void TerrainGame::updateObjectPhysics'))
  s=s[:pos]+'  { CB::SolverScope solverScope;\n'+s[pos:]
  marker='  // --- 5.';pos=s.index(marker,pos)
  s=s[:pos]+'  }\n'+s[pos:]
 if args.temporal_comparison:
  # The outer solver scope already brackets section 4, including history/islands.
  for marker,end,stage in [('  // --- 1.','  // --- 2.',0),('  // --- 2.','  // --- 3.',1),('  // --- 3.','  { CB::SolverScope',2)]:
   a=s.index(marker,s.index('void TerrainGame::updateObjectPhysics'));b=s.index(end,a)
   s=s[:a]+'  { CB::StageScope phaseScope('+str(stage)+');\n'+s[a:b]+'  }\n'+s[b:]
  # Section 1 declares variables/lambdas used below, so use an explicit boundary
  # timer rather than a lexical scope that would hide them.
  s=s.replace('  { CB::StageScope phaseScope(0);\n','  unsigned predictionStart=CB::ticks();\n')
  boundary=s.index('  // --- 2.',s.index('void TerrainGame::updateObjectPhysics'))
  brace=s.rfind('  }\n',0,boundary);s=s[:brace]+s[brace:].replace('  }\n','  CB::stageTicks[0]+=CB::ticks()-predictionStart;\n',1)
  # Body grid and contacts are separate scopes. Queries/SAT are nested children.
  a=s.index('  bodyPairs.commit();');b=s.index('  for (int si = 0; si < awakeCount; ++si)',a)
  s=s[:b]+'  CB::stageTicks[3]+=CB::ticks()-bodyGridStart;\n'+s[b:]
  s=s.replace('  bodyPairs.begin((int)bodies.size());','  unsigned bodyGridStart=CB::ticks();\n  bodyPairs.begin((int)bodies.size());')
  s=s.replace('      broadPhaseNear(area, bpNearPhys);','      {CB::StageScope q(4);broadPhaseNear(area,bpNearPhys);}')
  s=s.replace('    bodyPairs.queryBox(pairArea, nearBodies);','    {CB::StageScope q(4);bodyPairs.queryBox(pairArea,nearBodies);} CB::bodyCandidates+=nearBodies.size();')
  s=s.replace('if (!Tyra::collisionBoundsOverlap(A.bounds, Bs.bounds)) continue;','if (!Tyra::collisionBoundsOverlap(A.bounds, Bs.bounds)) {++CB::bodyBoundsRejects;continue;}')
  needle='    if (!Tyra::collisionBoundsOverlap(A.bounds,'
  s=s.replace(needle,'    CB::StageScope satScope(5);\n'+needle,1)
  marker='  // --- 5.';a=s.index(marker,s.index('void TerrainGame::updateObjectPhysics'))
  s=s[:a]+'  CB::StageScope writebackScope(6);\n'+s[a:]
  s=s.replace('    const float spin2 = rbDot(w, w);','    const float spin2 = rbDot(w, w);CB::energy+=speed2+spin2;')
  s=s.replace('      contacts.push_back(c);','      CB::maxDepth=std::max(CB::maxDepth,c.depth);\n      contacts.push_back(c);')
  if p.name=='game_physics.gen.cpp':
   needle='  if(warmStart)for(const auto& c:contacts)if(c.history>=0) {'
   s=s.replace(needle,'  CB::warmHits=warmStart?contactHistory.hits:0;CB::islands=useIslands?islands.count:0;\n'+needle)
 p.write_text(s,encoding='utf8',newline='\n')
p=proj/'src/terrain_game.cpp';s=p.read_text(encoding='utf8');s=s.replace('  const bool menuActive =','  benchTick();\n  const bool menuActive =',1);s=s.replace('  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);','  inputreplay::tick(engine, MULTIPLAYER_MODE != 0 ? &pad2 : nullptr);\n  {Tyra::PadButtons neutral{};engine->pad.setState(neutral,neutral,128,128,128,128);}',1);p.write_text(s,encoding='utf8',newline='\n')
p=proj/'src/gen/game_vehicles.gen.cpp';s=p.read_text(encoding='utf8');needle='    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {';assert s.count(needle)==1
s=s.replace(needle,'''      CB::carZ=v.pos[2];CB::carSpeed=v.speed;CB::carValid=1;
      inThrottle=CB::gas()?1.0F:0.0F;inBrake=0;inSteer=0;inHand=CB::gas()?0:1;inNos=0;
'''+needle)
s=s.replace('if (dx * dx + dz * dz < r * r && pushN < 8) pushIdx[pushN++] = oi;','if (dx * dx + dz * dz < r * r) {if(pushN<8)pushIdx[pushN++]=oi;else ++CB::pushDrops;}')
s=s.replace('        physPushAt(pushIdx[pk],','        ++CB::pushes;\n        physPushAt(pushIdx[pk],')
s=s.replace('    if (pushN > 0) {','    CB::pushCandidates += pushN;\n    if (pushN > 0) {')
s=s.replace('      for (int pk = 0; pk < pushN; ++pk) {','      for (int pk = 0; pk < pushN; ++pk) {\n        ++CB::pushTested;')
p.write_text(s,encoding='utf8',newline='\n')
p=proj/'src/scripts/district_mood.cpp';s=p.read_text(encoding='utf8');start=s.index('    const bool night =');end=s.index(';',start)+1;s=s[:start]+'    const bool night = false; // fixed daytime in both arms'+s[end:];p.write_text(s,encoding='utf8',newline='\n')
p=eng/'src/engine.cpp';s=p.read_text(encoding='utf8').replace('#include "engine.hpp"','#include "engine.hpp"\n#include "debug/collision_bench.hpp"');s=s.replace('void Engine::realLoop() {','void Engine::realLoop() {\n  CB::begin(renderer.core.getStallTotal());');p.write_bytes(s.encode())
# Silence normal chatter, preserve asserts/missing-resource errors.
p=eng/'inc/debug/debug.hpp';s=p.read_text(encoding='utf8').replace('#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")','#define TYRA_LOG(...) ((void)0) // measurement: no periodic console IO');p.write_bytes(s.encode())
# Automated physics controls do not need padman RPC, DMA polls or rumble.
# Keep this fixture-only: real games retain the normal physical Pad backend.
if args.temporal_comparison:
 p=eng/'src/pad/pad.cpp';s=p.read_text(encoding='utf8')
 for signature,body in [
  ('void Pad::init() {','\n  // CB synthetic input: no physical padman port or DMA buffer.\n  actuators=0;port=slot=0;buttons={};reset();resetJoys();return;\n'),
  ('void Pad::update() {','\n  // CB owns input; physical padman is outside this benchmark.\n  return;\n'),
  ('void Pad::setActuators(const bool& smallMotor, const u8& bigPower) {','\n  // CB owns input; do not send rumble RPCs.\n  return;\n')]:
  assert s.count(signature)==1;s=s.replace(signature,signature+body)
 p.write_bytes(s.encode('utf8'))
# Record the exact source heads and comparison scope.
meta={'baseline':subprocess.check_output(['git','-C',str(repo),'rev-parse',base],text=True).strip(),'candidate':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'data_layout':'shared candidate PhysBody/RbStep layout; renderer and assets identical','mesh_collision_objects':0,'tick_hz':294912000,'phases':28,'profiles':'release, NTSC progressive vsync on, day, all devkit polling off'}
meta['uncapped_baseline']=args.uncapped_baseline
meta['solver_comparison']=args.solver_comparison
meta['temporal_comparison']=args.temporal_comparison
meta['synthetic_pad']=args.temporal_comparison
meta['micro_comparison']=args.micro_comparison
if args.temporal_comparison:meta.update(phases=args.temporal_arms*14,temporal_arms=args.temporal_arms)
if args.solver_comparison and not args.temporal_comparison:meta['phases']=42
meta['settle_check']=args.settle_check
if args.settle_check:meta['phases']=args.temporal_arms*2
(root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
print('Instrumented same-ELF baseline/candidate arms:',meta)

if args.micro_comparison:
 # Same image for shadow validation and timing; only a boot-time file changes.
 (eng/'inc/debug/vu0_sat_probe.hpp').write_bytes((rig/'vu0_sat_probe.hpp').read_bytes())
 p=proj/'src/gen/game_physics.gen.cpp';p.write_bytes(b'#define TYRA_PHYSICS_MICRO_VERIFY 1\n'+p.read_bytes())
 p=eng/'src/physics/vu0_sat.cpp';p.write_bytes(b'#define TYRA_VU0_SAT_PROFILE 1\n'+p.read_bytes())
 p=extra;s=p.read_text(encoding='utf8');s='#include "debug/vu0_sat_probe.hpp"\n'+s
 s=s.replace('checked=true;if(!TyraSolverProbe::run())','checked=true;FILE* verify=fopen("sat-verify.cfg","r");Tyra::vu0SatVerify=verify!=nullptr;if(verify)fclose(verify);if(Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)CB::phase=12;printf("VUSAT mode=%s\\n",Tyra::vu0SatVerify?"verify":"timing");if(!TyraSolverProbe::run() || !Vu0SatProbe::run())')
 s=s.replace('CB::phase>=CB::phases','CB::phase>=((Tyra::vu0SatVerify && !TYRA_TEMPORAL_SETTLE)?14:CB::phases)')
 s=s.replace('  CB::configure();int found=0;','  CB::configure();Tyra::vu0SatMetrics={};Tyra::vu0SatValidationErrors=0;Tyra::vu0SatValidation={};int found=0;')
 p.write_bytes(s.encode())
 p=eng/'inc/debug/collision_bench.hpp';s='#include "physics/vu0_sat.hpp"\n'+p.read_text(encoding='utf8')
 code='''  char microName[64];snprintf(microName,sizeof(microName),"vu0-phase-%02d.csv",phase);
  if(FILE* m=fopen(microName,"w")) {
    const auto& v=Tyra::vu0SatMetrics;
    const auto& d=Tyra::vu0SatValidation;
    fprintf(m,"phase,jobs,batches,busy_finish,busy_ee_query,wait_ticks,validation_errors,numerical_differences,extra_touches,missed_reference_hits,max_depth_ratio,max_point_ratio,max_normal_error,max_extra_depth_ratio\\n%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g\\n",phase,v.jobs,v.batches,v.busyAtFinish,v.busyWhileQuerying,v.waitTicks,Tyra::vu0SatValidationErrors,d.numerical,d.extraTouches,d.misses,d.depthRatio,d.pointRatio,d.normalError,d.extraDepthRatio);fclose(m);
    printf("VUSAT phase=%d jobs=%u batches=%u busy_ee=%u benign=%u extra_touch=%u missed=%u errors=%u\\n",phase,v.jobs,v.batches,v.busyWhileQuerying,d.numerical,d.extraTouches,d.misses,Tyra::vu0SatValidationErrors);fflush(stdout);
  }
'''
 assert '  used = 0;\n}' in s;s=s.replace('  used = 0;\n}',code+'  used = 0;\n}')
 p.write_bytes(s.encode())

shot=(repo/'tools/ps2rig/cld_shot.hpp').read_text(encoding='utf8').replace('#include "debug/cld_probe.hpp"','#include "debug/collision_bench.hpp"')
shot=shot[:shot.index('inline void maybe(')]+'}\n'
(eng/'inc/debug/collision_shot.hpp').write_bytes(shot.encode())
p=proj/'src/terrain_game.cpp';text=p.read_text(encoding='utf8');text='#include "debug/collision_shot.hpp"\n'+text
text=text.replace('  engine->renderer.endFrame();','  engine->renderer.endFrame();\n  if(CB::ready && CB::frame==120 && ((TYRA_TEMPORAL_COMPARISON && (CB::phase<TYRA_TEMPORAL_ARMS || (!TYRA_TEMPORAL_SETTLE && CB::phase>=6*TYRA_TEMPORAL_ARMS && CB::phase<7*TYRA_TEMPORAL_ARMS))) || (!TYRA_TEMPORAL_COMPARISON && (CB::phase==0 || CB::phase==1 || (!TYRA_SOLVER_COMPARISON && (CB::phase==12 || CB::phase==13)) || (TYRA_SOLVER_COMPARISON && (CB::phase==2 || CB::phase==18 || CB::phase==19 || CB::phase==20)))))) {char name[64];snprintf(name,sizeof(name),"collision-photo-%02d.tga",CB::phase);CldShot::grab(engine,name);g_frameClockLast=0;}',1)
p.write_text(text,encoding='utf8',newline='\n')
