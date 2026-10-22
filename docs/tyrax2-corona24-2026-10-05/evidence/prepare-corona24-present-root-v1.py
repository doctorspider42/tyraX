from pathlib import Path
import json,shutil,hashlib
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');src=lab/'corona24-probe-physical-v1';out=lab/'corona24-probe-physical-v2';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((src/'target-source-manifest.json').read_text())
for name,h in m['files'].items():
 p=out/name;p.parent.mkdir(parents=True,exist_ok=True);assert sha(src/name)==h;shutil.copy2(src/name,p)
for name in ('res','.res-baked'):shutil.copytree(src/'game'/name,out/'game'/name)
p=out/'game/inc/pool2_static_probe.hpp';t=p.read_text();old='pool2ProbeReady=0x504f4f32u;pool2ProbeHalt();';assert t.count(old)==1
t=t.replace('#include "renderer/core/gs/renderer_core_depth.hpp"','#include "renderer/core/gs/renderer_core_depth.hpp"\n#include <graph.h>')
t=t.replace(old,'pool2ProbeReady=0x504f4f32u;\n  // Off-clock observer: let the finished framebuffer reach display before pausing.\n  for(unsigned observer=0;observer<3;++observer)graph_wait_vsync();\n  pool2ProbeHalt();')
p.write_text(t);m['files'][str(p.relative_to(out))]=sha(p);m['status']='ROOT_FROZEN_CORONA24_PRESENT_OBSERVER_DIAGNOSTIC'
manifest=out/'target-source-manifest.json';manifest.write_text(json.dumps(m,indent=2)+'\n')
(out/'root-source-freeze.json').write_text(json.dumps(dict(sourceManifestSha256=sha(manifest),sourceFiles=502,diagnosticOnly=True,baseManifestSha256=sha(src/'target-source-manifest.json'),onlyDelta='game/inc/pool2_static_probe.hpp',offClockObserverVblanks=3),indent=2)+'\n')
print(out,sha(manifest))
