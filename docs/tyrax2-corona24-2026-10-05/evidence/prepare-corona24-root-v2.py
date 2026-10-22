from pathlib import Path
import json,hashlib,re,shutil,subprocess
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001')
src=lab/'wild-gs-sprite-corona-physical-v2';out=lab/'corona24-probe-physical-v1'
assert out.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=json.loads((src/'target-source-manifest.json').read_text())
renderer=out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
t=renderer.read_text();old='RendererCoreDepth::bits==16';assert t.count(old)==1
renderer.write_text(t.replace(old,'(RendererCoreDepth::bits==16 || RendererCoreDepth::bits == 24)'),newline='\n')
tc=out/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp'
t=tc.read_text()
for a,b in [('sub.z   vf00, outputStq1, i','sub.z   color3, outputStq1, i'),('sub.z   vf00, vertex3, outputStq1','sub.z   color3, vertex3, outputStq1'),('sub.xy  vf00, outputStq3, vertex3','sub.xy  color3, outputStq3, vertex3')]:
 assert t.count(a)==1,a;t=t.replace(a,b)
tc.write_text(t,newline='\n')
patch=out/'tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/corona_flag_interlock.py'
patch.write_text('''"""Private TC-only paired upper VF dependency; fail closed on changed schedule."""
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);lines=p.read_text().splitlines();changed=[]
start=next(i for i,s in enumerate(lines)if s.strip()=="coronaQuadCheck:")
end=next(i for i,s in enumerate(lines)if s.strip()=="coronaDone:")
for i in range(start,end):
 if not re.search(r"\\bfmand\\b",lines[i]):continue
 assert re.match(r"\\s*nop\\s+fmand",lines[i]),lines[i]
 m=re.match(r"\\s*(subi?|sub)\\.([xyzw]+)\\s+(VF[0-9]+),",lines[i-1]);assert m,lines[i-1]
 mask,reg=m.group(2,3);assert reg!="VF00",lines[i-1]
 lines[i]=re.sub(r"nop(?=\\s+fmand)",f"abs.{mask} VF00, {reg}",lines[i],count=1)
 changed.append(dict(line=i+1,mask=mask,source=reg))
assert len(changed)==6,changed
p.write_text("\\n".join(lines)+"\\n")
print("PRIVATE_CORONA_PAIRED_ABS_INTERLOCK",changed)
''',newline='\n')
make=out/'tyra/Makefile.base';t=make.read_text();old='\t$(VCL) $@.vcl > $@.vsm\n';assert t.count(old)==1
new=old+'\t@if [ "$(notdir $<)" = "stapip_cull_tc_vu1.vclpp" ]; then python3 "$(SRCDIR)/renderer/3d/pipeline/static/core/programs/cull/corona_flag_interlock.py" "$@.vsm"; fi\n'
make.write_text(t.replace(old,new),newline='\n')
# Diagnostic keeps the original Hybrid main; changes only header and terrain shim.
diag=lab/'wild-gs-sprite-corona-probe-design-v2/source'
for name in ('game/inc/pool2_static_probe.hpp','game/src/terrain_game.cpp'):
 p=out/name;shutil.copy2(diag/name,p)
p=out/'game/inc/pool2_static_probe.hpp';t=p.read_text().replace('bits!=16','bits!=24').replace('depth_not_16','depth_not_24').replace('depth=16','depth=24');p.write_text(t,newline='\n')
files={name:sha(out/name) for name in base['files']}
for name in ('game/inc/pool2_static_probe.hpp',str(patch.relative_to(out))):files[name]=sha(out/name)
assert len(files)==502
manifest=out/'target-source-manifest.json';manifest.write_text(json.dumps(dict(frozen=True,status='ROOT_FROZEN_CORONA24_DIAGNOSTIC_SOURCE',files=dict(sorted(files.items()))),indent=2)+'\n')
(out/'root-source-freeze.json').write_text(json.dumps(dict(sourceManifestSha256=sha(manifest),sourceFiles=502,diagnosticOnly=True,baseManifestSha256=sha(src/'target-source-manifest.json')),indent=2)+'\n')
print(out,sha(manifest))
