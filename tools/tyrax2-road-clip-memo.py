"""Private exact-key road clip-plane reuse; fixed interleave, no timed clocks."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day'],
               check=True,env={**os.environ,'PYTHONUTF8':'1'})


def edit(path, old, new):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==1,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())


engine=out/'tyra/engine'
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(out/'game/inc/terrain_config.hpp','constexpr int INTERLEAVE_PASSES = 1;',
     'constexpr int INTERLEAVE_PASSES = 2;')
edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;','part.envTexBag->paintVu = true;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  StaPipBag();','  bool diagnosticRoad = false;\n  StaPipBag();')
proc=next(p for p in (out/'game/src').rglob('*.cpp') if '    c.colors.bind(c.colorBag);' in p.read_text(encoding='utf-8-sig'))
edit(proc,'    c.colors.bind(c.colorBag);',
     '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticRoad=c.owner==-3;')
header=engine/'inc/debug/motion_probe.hpp'
edit(header,'inline bool observing(){return false;}',
     'inline U memoCalls=0,memoHits=0,memoChecks=0;\ninline bool observing(){return false;}')
edit(engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp',
     '  void computeClipObjectSpacePlanes(const M4x4& mvp);', '''  bool diagnosticRoadCall=false;
  bool roadClipMemoValid=false;
  M4x4 roadClipMemoMvp;
  float roadClipMemoNear=0,roadClipMemoFar=0;
  Plane roadClipMemoPlanes[8];
  void computeClipObjectSpacePlanes(const M4x4& mvp);''')
edit(core,'  MotionProbe::add(MotionProbe::Bags);',
     '  diagnosticRoadCall=bag->diagnosticRoad;\n  MotionProbe::add(MotionProbe::Bags);')
edit(core,'  const float b = VU1_CLIP_XY_BAND;', '''  const bool memoEnabled=diagnosticRoadCall && MotionProbe::variant()==6;
  const bool memoWitness=diagnosticRoadCall && MotionProbe::offset>=1000 && MotionProbe::offset<1100;
  if(memoWitness) ++MotionProbe::memoCalls;
  const bool memoHit=memoEnabled && roadClipMemoValid &&
      memcmp(roadClipMemoMvp.data,mvp.data,sizeof(mvp.data))==0 &&
      memcmp(&roadClipMemoNear,&nearZ,sizeof(float))==0 &&
      memcmp(&roadClipMemoFar,&farZ,sizeof(float))==0;
  if(memoHit) {
    for(u8 i=0;i<8;++i) clipObjectSpacePlanes[i]=roadClipMemoPlanes[i];
    if(memoWitness) ++MotionProbe::memoHits;
    else return;
  }
  const float b = VU1_CLIP_XY_BAND;''')
anchor='''                   p[3] * m[15];
  }
}'''
edit(core,anchor,'''                   p[3] * m[15];
  }
  if(memoHit && memoWitness) {
    for(u8 i=0;i<8;++i) {
      TYRA_ASSERT(memcmp(&clipObjectSpacePlanes[i].normal,&roadClipMemoPlanes[i].normal,sizeof(Vec4))==0 &&
                  memcmp(&clipObjectSpacePlanes[i].distance,&roadClipMemoPlanes[i].distance,sizeof(float))==0,
                  "Road clip memo differs from original computation");
    }
    ++MotionProbe::memoChecks;
  }
  if(memoEnabled && !memoHit) {
    roadClipMemoMvp=mvp;roadClipMemoNear=nearZ;roadClipMemoFar=farZ;
    for(u8 i=0;i<8;++i) roadClipMemoPlanes[i]=clipObjectSpacePlanes[i];
    roadClipMemoValid=true;
  }
}''')
edit(out/'game/src/terrain_game.cpp',
     '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {',
     '''  if(MotionProbe::offset==1000) MotionProbe::memoCalls=MotionProbe::memoHits=MotionProbe::memoChecks=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    const bool memo=MotionProbe::variant()==6;
    if(!MotionProbe::memoCalls || (memo ? (!MotionProbe::memoHits || MotionProbe::memoChecks!=MotionProbe::memoHits)
                                      : (MotionProbe::memoHits || MotionProbe::memoChecks))) MotionProbe::valid=false;
    printf("LOG: ROADMEMO phase=%u calls=%u hits=%u checks=%u interleave=%u\\n",
      MotionProbe::phase,MotionProbe::memoCalls,MotionProbe::memoHits,MotionProbe::memoChecks,unsigned(INTERLEAVE_PASSES));''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-clip-plane-memo',
    modes=[0,6,0],fixedInterleave=2,constantVuPaint=True,
    key='all MVP bits plus computed near/far bits; per Core storage; road calls only',
    witness='cold offsets 1000..1099 recompute every hit using original arithmetic',
    sourceProject=str(source),files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
