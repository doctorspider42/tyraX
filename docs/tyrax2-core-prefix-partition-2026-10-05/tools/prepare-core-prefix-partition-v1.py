from pathlib import Path
import json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001')
source=(b/'prepare-night-producer-probe-v1.py').read_text(encoding='utf8')
head=source[:source.index("g='game/src/gen/game_lighting.gen.cpp'")].replace('night-producer-probe-physical-v2','core-prefix-partition-physical-v1').replace('kind==13','kind==14').replace('kind=13','kind=14').replace('PRIVATE producer observer','PRIVATE Core prefix partition observer').replace('producerEnabled&&o>=800','producerEnabled&&index<5400&&o>=800')
exec(compile(head,str(Path(__file__)),'exec'))
g='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(g,'#include "debug/hardware_trace.hpp"','#include "debug/hardware_trace.hpp"\n#include "debug/night_ablation.hpp"')
edit(g,'void StaPipCore::render(StaPipBag* bag) {','void StaPipCore::render(StaPipBag* bag) {\n  NightAblation::ProducerScope prefixBounds(0);\n  NightAblation::producerCount(0,1,bag->count>0?bag->count:0);')
edit(g,'  if (bag->count <= 0) {','  if (bag->count <= 0) {\n    NightAblation::producerCount(0,2);')
edit(g,'      recordOutsideBag(bag);','      NightAblation::producerCount(0,3);\n      recordOutsideBag(bag);')
edit(g,'  TYRA_ATTRIB_MARK(attribTextureStart);','  prefixBounds.stop();\n  NightAblation::ProducerScope prefixFacts(1);\n  NightAblation::producerCount(1,1,bag->count);\n  TYRA_ATTRIB_MARK(attribTextureStart);')
edit(g,'  const bool wantsLightPick = !bag->lighting && bag->info->dynLightPick;','  const bool wantsLightPick = !bag->lighting && bag->info->dynLightPick;\n  NightAblation::producerCount(1,2,wantsLightPick?1:0);\n  NightAblation::producerCount(1,3,blssOn?1:0);\n  NightAblation::producerCount(1,4,bag->lighting?1:0);')
edit(g,'  TYRA_ATTRIB_MARK(attribObjectDataStart);','  prefixFacts.stop();\n  NightAblation::ProducerScope prefixUniforms(2);\n  NightAblation::producerCount(2,1,bag->count);\n  TYRA_ATTRIB_MARK(attribObjectDataStart);')
edit(g,'  TYRA_ATTRIB_MARK(attribReplayStart);','  prefixUniforms.stop();\n  TYRA_ATTRIB_MARK(attribReplayStart);')
for n in m['files']:m['files'][n]=sha(out/n)
m['files']=dict(sorted(m['files'].items()));(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode())
(out/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(out/'target-source-manifest.json'),sourceFiles=501,changes=sorted(changes),baseManifestSha256=sha(base/'target-source-manifest.json'),timedExtraClocks='two per entered disjoint Core prefix scope; only800..1119',scopeNames=['headBoundsPackager','textureProgramLightFacts','objectDataRoute','unused','unused'],commonCodeFootprintUnpriced=True,allGameSourcesUnchanged=True,existingWaitsRetained=True),indent=2)+'\n').encode());print(out)
