"""Scratch-only visual A/B; deliberately NOT a performance measurement.

After a full bake/build with Live Debugger enabled, run this script then
native-build. Write `LOD X Z HEADING_DEG [CELL]` to bin/forest-visual.txt and capture
with the editor's --capture-frame. Camera follows terrain height + 1.8.
The deterministic 8000-placement subset matches instrument-density.py.
Restore clean sources and refresh mtimes before a normal playable build.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
if root==Path(__file__).resolve().parents[1]:
    raise SystemExit('Use a generated scratch project')
p=root/'src/terrain_game.cpp';s=p.read_text()
if 'forestbench' in s or 'FOREST_VISUAL' in s:
    raise SystemExit('Restore clean generated source first')
anchor='  if (scriptCtx.cameraOverride) {'
assert s.count(anchor)==1
probe=r'''
  // FOREST_VISUAL: host polling is for pictures only, never FPS measurements.
  static bool forestReady=false;
  static int visualFrame=0;
  static float visualLod=35,visualX=0,visualZ=0,visualHeading=90,visualCell=105;
  if(!forestReady) {
    auto rank=[](float x,float z){unsigned h=unsigned(int(x*100))*73856093u ^ unsigned(int(z*100))*19349663u;h^=h>>16;h*=0x7feb352du;h^=h>>15;return h;};
    std::stable_sort(procInsts.begin(),procInsts.end(),[&](const ProcInst&a,const ProcInst&b){return rank(a.x,a.z)<rank(b.x,b.z);});
    if(procInsts.size()>8000)procInsts.resize(8000);
    procInstCells.clear();procFinishInstances(-4,105.0F,210.0F,0);
    forestReady=true;
  }
  if(visualFrame++%30==0) {
    FILE*f=fopen("host:forest-visual.txt","r");
    if(f){float ld,x,z,h,cell=105;if(fscanf(f,"%f %f %f %f %f",&ld,&x,&z,&h,&cell)>=4 && ld>=10 && ld<=100 && cell>=20 && cell<=210){
      if(ld!=visualLod || x!=visualX || z!=visualZ || h!=visualHeading || cell!=visualCell)
        TYRA_LOG("FOREST_VISUAL lod=",ld," x=",x," z=",z," heading=",h," cell=",cell);
      if(cell!=visualCell){procInstCells.clear();procFinishInstances(-4,cell,210.0F,0);visualCell=cell;}
      visualLod=ld;visualX=x;visualZ=z;visualHeading=h;
    }fclose(f);}
    for(auto&p:procInsts)if(p.lodModel>=0 && p.model>=0 && strstr(MODEL_PATHS[p.model],"forest-oak-"))p.lodDist2=visualLod*visualLod;
  }
  const float visualAngle=visualHeading*0.01745329252F;
  scriptCtx.cameraOverride=true;
  scriptCtx.cameraEye=Vec4(visualX,terrainHeightAt(visualX,visualZ)+1.8F,visualZ,1);
  scriptCtx.cameraAt=scriptCtx.cameraEye+Vec4(sinf(visualAngle)*20,0,cosf(visualAngle)*20,0);
'''
p.write_text(s.replace(anchor,probe+'\n'+anchor))
print('Visual comparison instrumented:',root)
