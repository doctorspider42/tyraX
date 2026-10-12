"""Private road-free hitch localization; control/scoped/control, no added fences."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = map(lambda p: Path(p).resolve(), sys.argv[1:3])
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-reflection-isolation.py'),
                str(out), str(source)], check=True)
spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)

def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())

p = out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(p, 'inline constexpr U phaseCount=9;', 'inline constexpr U phaseCount=3;')
edit(p, 'constexpr U modes[]={0,1,2,3,0,3,2,1,0};', 'constexpr U modes[]={0,0,0};')
edit(p, 'current.z>=108.0F', 'current.z>=-40.0F')
edit(p, 'current.z<108.0F', 'current.z<-40.0F')
helper = r'''
struct HitchEvent { U offset,begin,duration; int object; float z; const char* label; };
inline HitchEvent hitchEvents[8192]{};
inline U hitchSize=0,hitchDropped=0;
inline bool hitchActive(){return phase==1 && sample && gas() && current.z>=-58.0F && current.z<=-49.0F;}
inline void hitchEnd(const char* label,int object,U stamp){
  if(!hitchActive())return;
  const U end=ticks();
  if(hitchSize==8192){++hitchDropped;return;}
  hitchEvents[hitchSize++]={offset,stamp-start,end-stamp,object,current.z,label};
}
struct HitchScope {
  const char* label; int object; U stamp; bool active;
  HitchScope(const char* l,int o=-1):label(l),object(o),stamp(0),active(hitchActive()) {if(active)stamp=ticks();}
  ~HitchScope(){if(active)hitchEnd(label,object,stamp);}
};
inline void hitchFlush(){
  if(phase!=1)return;
  FILE* f=fopen("host:hitch-events.csv","w");
  if(!f){valid=false;return;}
  fprintf(f,"phase,offset,begin,duration,object,z,label\n");
  for(U i=0;i<hitchSize;++i){const auto& e=hitchEvents[i];
    fprintf(f,"%u,%u,%u,%u,%d,%.6f,%s\n",phase,e.offset,e.begin,e.duration,e.object,e.z,e.label);}
  if(ferror(f))valid=false;
  if(fclose(f)!=0 || hitchDropped || !hitchSize)valid=false;
  printf("LOG: HITCH events=%u dropped=%u bytes=%u\n",hitchSize,hitchDropped,unsigned(sizeof(hitchEvents)));
}
'''
edit(p, 'inline void end(U pacing) {', helper+'\ninline void end(U pacing) {')
edit(p, '} else if(finishPending) {', '} else if(finishPending) {\n    hitchFlush();')
physics = out/'game/src/gen/game_physics.gen.cpp'
edit(physics, 'return (costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0;',
     'return (MotionProbe::hitchActive() || costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0;')
edit(physics, 'auto costEnd = [&](const char* label, int object, u32 start) {',
     'auto costEnd = [&](const char* label, int object, u32 start) {\n    MotionProbe::hitchEnd(label,object,start);')

def scope(path, method, label, obj='-1'):
    text=path.read_text(encoding='utf-8-sig')
    if '#include "debug/motion_probe.hpp"' not in text:
        text='#include "debug/motion_probe.hpp"\n'+text
    first,_=quiet.body_span(text,method)
    text=text[:first+1]+f'\n  MotionProbe::HitchScope hitchScope("{label}",{obj});'+text[first+1:]
    path.write_bytes(text.encode())

for method in ('buildTerrainChunk','updateTerrainChunks','renderTerrain'):
    scope(physics,'TerrainGame::'+method,method,'slot' if method=='buildTerrainChunk' else '-1')
collision=out/'game/src/gen/game_collision.gen.cpp'
for method in ('rebuildObjectGeometry','applyGeoLod'):
    scope(collision,'TerrainGame::'+method,method,'index')
game=out/'game/src/terrain_game.cpp'
preload='--preload' in sys.argv
all_pins='--all-pins' in sys.argv
production_pins='--production-pins' in sys.argv or all_pins
assert not (preload and production_pins)
if production_pins:
    if not all_pins:
        edit(physics,'  prepareGroundShadowTextures();',
             '  if(MotionProbe::phase==1) prepareGroundShadowTextures();')
    edit(game,'void TerrainGame::loop() {', '''void TerrainGame::loop() {
  if(MotionProbe::offset==1100) {
    const bool pinned=!groundShadowPins.empty();
    const bool expected=MotionProbe::phase==1;
    if(pinned!=expected)MotionProbe::valid=false;
    printf("LOG: GROUNDPINS phase=%u count=%u valid=%u\\n",MotionProbe::phase,
           unsigned(groundShadowPins.size()),unsigned(pinned==expected));
  }''')
    if all_pins:
        edit(game,'const bool expected=MotionProbe::phase==1;','const bool expected=true;')
        edit(p,'current.z>=-40.0F','current.z>=108.0F')
        edit(p,'current.z<-40.0F','current.z<108.0F')
if preload:
    edit(game,'void TerrainGame::loop() {', '''void TerrainGame::loop() {
  if(MotionProbe::phase==1 && MotionProbe::offset==1100) {
    const bool a=acquireTexture("gshadow/s0_3_4.png")!=nullptr;
    const bool b=acquireTexture("gshadow/s0_0_4.png")!=nullptr;
    if(!a || !b)MotionProbe::valid=false;
    printf("LOG: HITCHPRELOAD valid=%u offset=1100 phase=1\\n",unsigned(a && b));
  }
  if(MotionProbe::phase==2 && MotionProbe::offset==0) {
    releaseTexture("gshadow/s0_3_4.png");
    releaseTexture("gshadow/s0_0_4.png");
  }''')
edit(game,'Texture* TerrainGame::acquireTexture(const std::string& path) {',
     'Texture* TerrainGame::acquireTexture(const std::string& path) {\n  MotionProbe::HitchScope hitchScope("acquireTexture");')
for call,label in [('engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));','beginFrame'),
                   ('engine->renderer.endFrame();','endFrame')]:
    edit(game,call,'{ MotionProbe::HitchScope hitchScope("'+label+'"); '+call+' }')
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='road-free-hitch-localization',
    sourceProject=str(source),modes=['control','local-scopes','control'],
    window=[-58,-49],endpoint=108 if all_pins else -40,preload=preload,
    productionPins=production_pins,allPins=all_pins,
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
