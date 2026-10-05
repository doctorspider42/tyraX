from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');base=lab/'wild-gs-sprite-corona-physical-v2';fixed=lab/'corona24-probe-physical-v1';out=lab/'corona24-pricing-physical-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((base/'target-source-manifest.json').read_text())
for name,h in m['files'].items():
 assert sha(base/name)==h;dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/name,dst)
changes=['tyra/Makefile.base','tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp','tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp','tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/corona_flag_interlock.py']
for name in changes:shutil.copy2(fixed/name,out/name);m['files'][name]=sha(out/name)
for name in ('res','.res-baked'):shutil.copytree(base/'game'/name,out/'game'/name)
assert len(m['files'])==501 and 'game/inc/pool2_static_probe.hpp' not in m['files']
m['status']='ROOT_FROZEN_CORONA24_ORDINARY_PRICING_SOURCE';m['files']=dict(sorted(m['files'].items()))
p=out/'target-source-manifest.json';p.write_bytes((json.dumps(m,indent=2)+'\n').encode())
(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(p),sourceFiles=501,pricingOnly=True,baseManifestSha256=sha(base/'target-source-manifest.json'),engineDeltas=changes,gameSourceUnchanged=True),indent=2)+'\n').encode())
print(out,sha(p))
