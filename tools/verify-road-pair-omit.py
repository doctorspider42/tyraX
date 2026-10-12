"""Host check of P07's actual private gate and its pre-allocation insertion."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
root=Path(sys.argv[1]);engine=root/'tyra/engine'
header=(engine/'inc/debug/motion_probe.hpp').read_text()
body=header[header.index('inline U pairEligible='):header.index('inline bool observing()')]
test='''#include <cassert>
using U=unsigned;U offset=0,mode=0;U variant(){return mode;}
'''+body+'''
int main(){
for(U m:{0u,6u})for(U id:{0u,23u,24u,34u,35u})for(U t:{999u,1000u,1099u,1100u,1335u}){
mode=m;offset=t;pairEligible=pairSkipped=pairTriangles=0;
bool selected=id==24||id==34,skip=selected&&mode==6,cold=t>=1000&&t<1100;
assert(omitPairClip(id,64)==skip);
assert(pairEligible==unsigned(selected&&cold));assert(pairSkipped==unsigned(skip&&cold));
assert(pairTriangles==64*unsigned(selected&&cold));
}}
'''
test='#include <initializer_list>\n'+test
core=(engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
needle='if(MotionProbe::omitPairClip(packages[i].bag->diagnosticRoadId,triangles))continue;'
assert core.count(needle)==1
assert core.index(needle)<core.index('for (u32 t = 0; t < triangles; t += trisPerChunk) {')
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(test)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
result=dict(passed=True,cases=50,gateSha256=hashlib.sha256(body.encode()).hexdigest(),
 scope='destructive ID/mode/cold-window gate, before qbuffer allocation; not raster equivalence')
(root/'pair-omit-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
