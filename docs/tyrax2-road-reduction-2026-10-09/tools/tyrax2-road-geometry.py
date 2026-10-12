"""A05 unchanged road-stream capture, one phase, no performance claim."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out,source=map(lambda p:Path(p).resolve(),sys.argv[1:3])
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-sky-bounds.py'),str(out),str(source)],check=True)
def edit(p,old,new):
    s=p.read_text(encoding='utf-8-sig');assert s.count(old)==1,(p,old,s.count(old))
    p.write_bytes(s.replace(old,new).encode())
h=out/'tyra/engine/inc/debug/motion_probe.hpp'
edit(h,'inline constexpr U phaseCount=6;','inline constexpr U phaseCount=1;')
edit(h,'constexpr U modes[]={0,6,0,0,6,0};','constexpr U modes[]={0};')
game=out/'game/src/terrain_game.cpp'
edit(game,'  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {', '''  if(MotionProbe::phase==0 && MotionProbe::offset==1050) {
    FILE* f=fopen("host:road-geometry.bin","wb");
    unsigned chunks=0,vertices=0;bool ok=f!=nullptr;
    for(const auto& c:procChunks)if(c.owner==-3)++chunks;
    const unsigned head[2]={0x31414452,chunks};
    if(f){
      ok=fwrite(head,sizeof(head),1,f)==1;
      unsigned ordinal=0;
      for(unsigned ci=0;ci<procChunks.size();++ci){
        const auto& c=procChunks[ci];if(c.owner!=-3)continue;
        const unsigned h[8]={ci,ordinal++,unsigned(c.vertices.size()),unsigned(c.stripRun),
          unsigned(c.roadBlend),unsigned(c.roadEdge),unsigned(c.sts.size()),unsigned(c.colors.size())};
        ok=ok && fwrite(h,sizeof(h),1,f)==1;
        if(c.vertices.size()!=c.sts.size() || c.vertices.size()!=c.colors.size()){ok=false;break;}
        for(unsigned i=0;i<c.vertices.size();++i){
          const auto& v=c.vertices[i];const auto& s=c.sts[i];const auto& col=c.colors[i];
          const float row[12]={v.x,v.y,v.z,v.w,s.x,s.y,s.z,s.w,col.r,col.g,col.b,col.a};
          ok=ok && fwrite(row,sizeof(row),1,f)==1;++vertices;
        }
      }
      if(fclose(f)!=0)ok=false;
    }
    printf("ROADGEOMETRY phase=%u offset=%u chunks=%u vertices=%u valid=%u\\n",
      MotionProbe::phase,MotionProbe::offset,chunks,vertices,unsigned(ok));
    if(!ok)MotionProbe::valid=false;
  }
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {''')
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'road-geometry-manifest.json').write_text(json.dumps(dict(experiment='A05-road-geometry-capture',
    captureOffset=1050,note='Unchanged drawing; one phase. Binary XYZ/STQ/colour with original chunk/run identity; no timing price.',
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
