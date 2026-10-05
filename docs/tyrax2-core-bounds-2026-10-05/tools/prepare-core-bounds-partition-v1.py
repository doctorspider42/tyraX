from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001')
source=(b/'prepare-night-producer-probe-v1.py').read_text(encoding='utf8')
head=source[:source.index("g='game/src/gen/game_lighting.gen.cpp'")].replace('night-producer-probe-physical-v2','core-bounds-partition-physical-v1').replace('kind==13','kind==15').replace('kind=13','kind=15').replace('PRIVATE producer observer','PRIVATE Core prefix partition observer').replace('producerEnabled&&o>=800','producerEnabled&&index<5400&&o>=800')
exec(compile(head,str(Path(__file__)),'exec'))

g='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(g,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(g,'void StaPipCore::render(StaPipBag* bag) {','void StaPipCore::render(StaPipBag* bag) {\n NightAblation::ProducerScope head(0);\n NightAblation::producerCount(0,1,bag->count>0?bag->count:0);')
edit(g,'  if (bag->count <= 0) {','  if (bag->count <= 0) {\n NightAblation::producerCount(0,2);')
edit(g,'  StaPipBagPackagesBBox* bbox = nullptr;','  head.stop(); NightAblation::ProducerScope boxes(1);\n NightAblation::producerCount(1,1,bag->count);\n NightAblation::producerCount(1,2,reuseTransform?1:0);\n StaPipBagPackagesBBox* bbox = nullptr;')
edit(g,'  CoreBBoxFrustum frustumCheck = OUTSIDE_FRUSTUM;','  boxes.stop(); NightAblation::ProducerScope planes(2);\n NightAblation::producerCount(2,1,bag->count);\n NightAblation::producerCount(2,2,frustumCull?1:0);\n CoreBBoxFrustum frustumCheck = OUTSIDE_FRUSTUM;')
edit(g,'      recordOutsideBag(bag);','      NightAblation::producerCount(2,3);\n recordOutsideBag(bag);')
edit(g,'  packager.setRenderBBox(bbox);','  planes.stop(); NightAblation::ProducerScope matrix(3);\n NightAblation::producerCount(3,1,bag->count);\n packager.setRenderBBox(bbox);')
edit(g,'#if TYRA_STAPIP_GUARD_BAND_BAGS','  matrix.stop(); NightAblation::ProducerScope clip(4);\n NightAblation::producerCount(4,1,bag->count);\n NightAblation::producerCount(4,2,classifyPackages?1:0);\n#if TYRA_STAPIP_GUARD_BAND_BAGS')
edit(g,'  TYRA_ATTRIB_MARK(attribTextureStart);','  clip.stop();\n TYRA_ATTRIB_MARK(attribTextureStart);')
for n in m['files']:m['files'][n]=sha(out/n)
m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=sorted(changes),baseManifestSha256=sha(base/'target-source-manifest.json'),timedExtraClocks='two per entered disjoint bounds scope; only800..1119',scopeNames=['headSizeTransformKey','bboxLookupSetSize','planesMainCull','mvpTransformCache','clipPlanesPackage'],commonCodeFootprintUnpriced=True,allGameSourcesUnchanged=True,existingWaitsRetained=True),indent=2)+'\n').encode());print(out)
