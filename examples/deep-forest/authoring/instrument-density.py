"""Instrument a GENERATED SCRATCH forest (never the authored example).

Usage: python instrument-density.py /absolute/scratch/project
Then invoke native-build directly; restore clean sources before a playable build.
One ELF, nested deterministic subsets, three frozen headings + a walking loop.
No network polling, engine modifications or per-instance density branch.
"""
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('project')
parser.add_argument('--counts',default='0,1000,2000,4000,6000,9000,12000,20000')
parser.add_argument('--lod',type=float,default=35)
parser.add_argument('--lods',help='Comma-separated oak-only LOD distances; rock LOD stays authored')
parser.add_argument('--route-radius',type=float,default=35)
parser.add_argument('--preserve-order',action='store_true',help='Use the authored graph prefix instead of ranked subsets')
parser.add_argument('--cell',type=float,default=105,help='Spatial culling cell size; does not change draw distance')
args=parser.parse_args()
counts=[int(n) for n in args.counts.split(',')]
assert counts and all(0<=n<=20000 for n in counts)
lods=[float(n) for n in args.lods.split(',')] if args.lods else [args.lod]
assert all(0<n<=210 for n in lods)
trials=len(counts)*len(lods)*4
root=Path(args.project).resolve()
if root == Path(__file__).resolve().parents[1]:
    raise SystemExit('Use a scratch copy, not the example')
header=r'''
#pragma once
namespace forestbench {
inline int frame=0, trial=-1, copies=0, trees=0, submitted=0, near=0, far=0;
inline float lod=35;
inline bool done=false;
inline unsigned tick(){unsigned t;__asm__ volatile("mfc0 %0, $9":"=r"(t));return t;}
inline unsigned rank(float x,float z){unsigned h=unsigned(int(x*100))*73856093u ^ unsigned(int(z*100))*19349663u;h^=h>>16;h*=0x7feb352du;h^=h>>15;return h;}
inline float periods[240], scenes[240];
inline unsigned long long submittedSum=0,nearSum=0,farSum=0;
inline int samples=0;
}
'''
(root/'inc/forest_bench.hpp').write_text(header)
p=root/'src/terrain_game.cpp';s=p.read_text()
if 'forest_bench.hpp' in s: raise SystemExit('Already instrumented')
s='#include "forest_bench.hpp"\n'+s
anchor='  if (scriptCtx.cameraOverride) {'
assert s.count(anchor)==1
probe=r'''
  using namespace forestbench;
  static std::vector<ProcInst> allForest;
  if(allForest.empty()) {
    allForest=procInsts;
    std::stable_sort(allForest.begin(),allForest.end(),[](const ProcInst&a,const ProcInst&b){return rank(a.x,a.z)<rank(b.x,b.z);});
  }
  static const int counts[]={0,1000,2000,4000,6000,9000,12000,20000};
  static const float lods[]={LOD_VALUES};
  const int trialNow=std::min(frame/300,31), local=frame%300;
  if(trialNow!=trial) {
    trial=trialNow; copies=std::min(counts[(trial/4)%COUNT_COUNT],(int)allForest.size());
    lod=lods[(trial/4)/COUNT_COUNT];
    procInsts.assign(allForest.begin(),allForest.begin()+copies);
    for(auto& p:procInsts) if(LOD_PREDICATE) p.lodDist2=lod*lod;
    procInstCells.clear(); procFinishInstances(-4,105.0F,210.0F,0);
    trees=0;for(const auto&p:procInsts)if(p.model>=0 && strstr(MODEL_PATHS[p.model],"forest-oak-"))++trees;
    samples=0;submittedSum=nearSum=farSum=0;
    TYRA_LOG("FOREST_BEGIN trial=",trial," copies=",copies," trees=",trees," pose=",trial%4," lod=",lod);
  }
  const int pose=trial%4;
  float a=pose*1.570796327F,x=0,z=0;
  if(pose==3){a=6.2831853F*std::max(0,local-60)/240.0F;x=35*sinf(a);z=35*cosf(a);a+=1.570796327F;}
  scriptCtx.cameraOverride=true;
  scriptCtx.cameraEye=Vec4(x,terrainHeightAt(x,z)+1.8F,z,1);
  scriptCtx.cameraAt=scriptCtx.cameraEye+Vec4(sinf(a)*20,0,cosf(a)*20,0);
  static unsigned prev=0;
  const unsigned now=tick();
  const float periodMs=(now-prev)/294912.0F;prev=now;
  if(!done && local>60 && samples<239) periods[samples]=periodMs;
  submitted=near=far=0;
'''
probe=probe.replace('0,1000,2000,4000,6000,9000,12000,20000',','.join(map(str,counts)))
probe=probe.replace('frame/300,31',f'frame/300,{trials-1}')
probe=probe.replace('COUNT_COUNT',str(len(counts))).replace('LOD_VALUES',','.join(f'{n:.6f}F' for n in lods))
probe=probe.replace('LOD_PREDICATE','p.lodModel>=0 && p.model>=0 && strstr(MODEL_PATHS[p.model],"forest-oak-")' if args.lods else 'p.lodModel>=0')
if args.preserve_order:
    # Runtime cells already sorted procInsts. Restore SOURCE order explicitly.
    s='#include <map>\n'+s
    probe=probe.replace('    std::stable_sort(allForest.begin(),allForest.end(),[](const ProcInst&a,const ProcInst&b){return rank(a.x,a.z)<rank(b.x,b.z);});',r'''
    std::map<std::pair<float,float>,int> sourceOrder;
    for(unsigned i=0;i<sizeof(PROC_INST_ROWS)/sizeof(PROC_INST_ROWS[0]);++i)sourceOrder[{PROC_INST_ROWS[i].x,PROC_INST_ROWS[i].z}]=i;
    std::stable_sort(allForest.begin(),allForest.end(),[&](const ProcInst&a,const ProcInst&b){return sourceOrder.at({a.x,a.z})<sourceOrder.at({b.x,b.z});});
''')
probe=probe.replace('105.0F,210.0F',f'{args.cell:.6f}F,210.0F')
probe=probe.replace('x=35*sinf(a);z=35*cosf(a);',f'x={args.route_radius:.6f}F*sinf(a);z={args.route_radius:.6f}F*cosf(a);')
s=s.replace(anchor,probe+'\n'+anchor)
# Time scene work with the same low-overhead COP0 instrument as the built-in HUD.
anchor='      renderScene();'
assert s.count(anchor)>=1
s=s.replace('  engine->renderer.beginFrame(CameraInfo3D', '  const unsigned sceneStart=forestbench::tick();\n  engine->renderer.beginFrame(CameraInfo3D',1)
s=s.replace('    drawDebugHud(engine, cameraPosition, cameraLookAt);','    const float benchSceneMs=(forestbench::tick()-sceneStart)/294912.0F;\n    if(!done && local>60 && samples<239) {scenes[samples]=benchSceneMs;submittedSum+=submitted;nearSum+=near;farSum+=far;++samples;}\n    drawDebugHud(engine, cameraPosition, cameraLookAt);',1)
finish=r'''
  if(!done && local==299) {
    float sum=0,sc=0;int misses=0;
    for(int i=0;i<samples;++i){sum+=periods[i];sc+=scenes[i];misses+=periods[i]>25.0F;}
    std::sort(periods,periods+samples);std::sort(scenes,scenes+samples);
    char row[512];snprintf(row,sizeof(row),"FOREST_RESULT trial=%d copies=%d trees=%d pose=%d n=%d fps=%.3f mean=%.3f p95=%.3f scene=%.3f scene95=%.3f misses=%d submitted=%.1f near=%.1f far=%.1f",trial,copies,trees,pose,samples,1000.0F*samples/sum,sum/samples,periods[samples*95/100],sc/samples,scenes[samples*95/100],misses,float(submittedSum)/samples,float(nearSum)/samples,float(farSum)/samples);
    TYRA_LOG(row);
    if(trial==31){done=true;TYRA_LOG("FOREST_DONE");}
  }
  if(!done)++frame;
'''
finish=finish.replace('trial==31',f'trial=={trials-1}')
finish=finish.replace('far=%.1f",','far=%.1f lod=%.1f",').replace('float(farSum)/samples);','float(farSum)/samples,lod);')
s=s.replace('  engine->renderer.endFrame();','  engine->renderer.endFrame();\n'+finish,1)
p.write_text(s)
# Diagnostic counts only: material bags and cell candidates are NOT called
# visible trees. Cost includes these three integer increments in every arm.
p=root/'src/gen/procedural.gen.cpp';s=p.read_text()
if 'void TerrainGame::renderProcInstances()' not in s:
    matches=[p for p in (root/'src').rglob('*.cpp') if 'void TerrainGame::renderProcInstances()' in p.read_text()]
    assert len(matches)==1
    p=matches[0];s=p.read_text()
s='#include "forest_bench.hpp"\n'+s
anchor='      int gn = 0;\n      const int g0 = procInstGeoRange(model, &gn);'
assert s.count(anchor)==1
s=s.replace(anchor,'      ++forestbench::submitted;\n      if(model==p.model)++forestbench::near;else ++forestbench::far;\n'+anchor)
p.write_text(s)
print('Instrumented',root)
