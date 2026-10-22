from pathlib import Path
import json,hashlib,shutil,datetime
b=Path('F:/Projects/tyrax2-lab-20261001');out=Path('docs/tyrax2-sky-retint-2026-10-05');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();bins=json.loads((out/'binary-hashes.json').read_text())
def cp(src,dst):
 if src.suffix in ('.elf','.sym'):bins[str(src)]=dict(sha256=sha(src),bytes=src.stat().st_size);return
 dst.parent.mkdir(parents=True,exist_ok=True)
 if dst.exists():assert sha(dst)==sha(src),str(dst)
 else:shutil.copyfile(src,dst)
def tree(src,dst):
 for p in src.rglob('*'):
  if p.is_file()and '__pycache__' not in p.parts:cp(p,dst/p.relative_to(src))
for name in ['night-ablation-ps2-sky-trigger-order1-20261005-retry1-launch','night-ablation-ps2-sky-trigger-order1-20261005-retry1-evidence','night-ablation-ps2-sky-trigger-order1-20261005-launch','sky-retint-production-v2-runtime','sky-retint-production-v3-runtime','sky-retint-fpp-syntax-v1','sky-retint-production-v1-compile-only']:
 tree(b/name,out/'runs'/name)
for name in ['sky-retint-production-native-v1.log','sky-retint-production-native-v1.err','sky-retint-production-native-v2.log','sky-retint-production-native-v2.err','sky-retint-production-native-v3.log','sky-retint-production-native-v3.err','sky-retint-fpp-codegen-v1.log']:
 cp(b/name,out/'builds'/name)
for name in ['sky-retint-physical-trigger-proof-v2.json','sky-retint-fpp-syntax-v1.py','sky-retint-production-v2-source-manifest.json','sky-retint-production-v3-source-manifest.json','sky-retint-production-v3-engine-manifest.json','sky-retint-production-v2-build-exit.json','sky-retint-production-v3-build-exit.json','sky-retint-production-v3-native-proof.json','sky-retint-production-qualify-v2.py','sky-retint-production-qualify-v3.py','sky-retint-production-native-audit-v3.py','archive-sky-retint-v1.py']:
 cp(b/name,out/'tools'/name)
g=b/'sky-retint-production-vehicle-v1/game';m=json.loads((b/'sky-retint-production-v3-source-manifest.json').read_text());old=json.loads((b/'sky-retint-production-v2-source-manifest.json').read_text())
for src,mapping in [(g,m),(b/'sky-retint-production-v2-compile-inputs',old)]:
 for n,d in mapping.items():assert sha(src/n)==d;cp(src/n,out/'source-blobs'/d)
e=Path('vendor/tyra/engine');em=json.loads((b/'sky-retint-production-v3-engine-manifest.json').read_text())
for n,d in em.items():assert sha(e/n)==d;cp(e/n,out/'source-blobs'/d)
proof=json.loads((b/'sky-retint-production-v3-runtime/proof.json').read_text());images=proof['images'];review=dict(status='PASS_ROOT_VISUAL_REVIEW_FOUR_OWNED_WARM_GENERATIONS',images={k:dict(file=v['image'],sha256=v['sha256'])for k,v in images.items()},observations='Forced RGB changes are deliberate; painted/plain sky, vehicle, terrain and HUD show no new stretched/missing triangles. Procedural view is first-person. Runtime proof remains unchanged; no physical warm-image confirmation inferred.',runtimeProofSha256=sha(b/'sky-retint-production-v3-runtime/proof.json'));(out/'tools/production-v3-visual-review.json').write_bytes((json.dumps(review,indent=2)+'\n').encode())
repo=Path('src/game_templates.inc').read_text(encoding='utf8');gen=(g/'src/gen/game_collision.gen.cpp').read_text();fpp=(b/'sky-retint-fpp-codegen-v1/sky-retint-fpp/src/gen/game_collision.gen.cpp').read_text()
def method(s):return s[s.index('bool TerrainGame::retintSkyDomeColors() {'):s.index('void TerrainGame::buildSkyDome() {')].strip()
assert method(repo)==method(gen)==method(fpp);record=dict(status='PASS_PRODUCTION_GENERATOR_ORBIT_FPP_METHOD_BYTE_EQUAL',methodSha256=hashlib.sha256(method(repo).encode()).hexdigest(),productionHasNoHarness='forcedSkyRgb' not in repo and 'SKYORACLE' not in repo);assert record['productionHasNoHarness'];(out/'tools/generator-method-proof.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
(out/'binary-hashes.json').write_bytes((json.dumps(bins,indent=2)+'\n').encode());m={str(p.relative_to(out)).replace('\\','/'):dict(sha256=sha(p),bytes=p.stat().st_size)for p in sorted(out.rglob('*'))if p.is_file()and p.name!='payload-manifest.json'};(out/'payload-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());print('Sealed payloads',len(m),'oracles',proof['oracleCalls'],'compared vertices',proof['comparedVertices'])
