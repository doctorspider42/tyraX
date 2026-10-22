"""Read-only confirmation: run ONLY after root's explicit native completion notification."""
from pathlib import Path
import argparse,hashlib,json,struct
ap=argparse.ArgumentParser();ap.add_argument('--fixture',required=True,type=Path);ap.add_argument('--audit',required=True,type=Path);ap.add_argument('--out',required=True,type=Path);a=ap.parse_args()
F=a.fixture;L=F.parent
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def wp(s):return Path('/mnt/'+s[0].lower()+s[2:].replace('\\','/')) if len(s)>2 and s[1]==':' else Path(s)
assert not a.out.exists()
prov=read(F/'root-native-provenance.json');ex=read(F/'root-native-command-exit.json');audit=read(a.audit);m=read(F/'target-source-manifest.json')
assert prov['build_exit_code']==ex['processExitCode']==0
assert audit['status'].startswith('PASS_') and not audit['blockers']
assert sha(F/'target-source-manifest.json')=='769f1403389da4d3683a17806fce1b75b381a4cf4f8229ee4d3306a44ca806de'
assert prov['target_source_manifest_sha256']==audit['sourceManifestSha256']==sha(F/'target-source-manifest.json')
review=read(L/'wild-pool2-ee-review-v1/source-review-owner-local-supplement.json')['reviewedCandidateFiles']
assert m['files']==review and len(review)==499
for rel,h in review.items():assert sha(F/rel)==h,rel
pins={}
for p,h in audit['inputPins'].items():q=wp(p);assert sha(q)==h,p;pins[p]=h
for field in ['objectPins','archives','externalDependencies']:
 for p,h in audit[field].items():assert sha(wp(p))==h,(field,p)
for rel,row in audit['actualSourceMirrors'].items():assert sha(wp(row['actualPath']))==row['sha256']==review[rel],rel
for dep in audit['dependencies']:
 assert wp(dep['object']).exists()
 for row in dep['dependencies']:assert sha(wp(row['path']))==row['sha256'],row['path']
def text_section(p):
 b=p.read_bytes();assert b[:6]==b'\x7fELF\x01\x01';off=struct.unpack_from('<I',b,32)[0];ent,n,idx=struct.unpack_from('<HHH',b,46);ss=[struct.unpack_from('<10I',b,off+i*ent) for i in range(n)];names=ss[idx];ns=b[names[4]:names[4]+names[5]]
 hit=[s for s in ss if ns[s[0]:].split(b'\0',1)[0]==b'.text'];assert len(hit)==1;s=hit[0];return b[s[4]:s[4]+s[5]]
e=F/'game/bin/vehicle-playground.elf';s=F/'game/bin/vehicle-playground.elf.sym'
assert sha(e)==prov['selected_elf_sha256']==audit['actualELFSha256'];assert sha(s)==prov['selected_symbol_sha256']==audit['actualSymbolSha256']
t=text_section(e);assert t==text_section(s);th=hashlib.sha256(t).hexdigest();assert th==audit['matchingTextSha256']
assets=read(F/'runtime-assets-manifest.json')['files'];base=read(L/'wild-pool-table-physical-v3/runtime-assets-manifest.json')['files'];assert assets==base and len(assets)==298
adpcm=[r for r in assets if r.endswith('.adpcm')];assert len(adpcm)==4
for rel,h in assets.items():assert sha(F/'game/bin'/rel)==h,rel
oracle=read(L/'wild-pool2-ee-producer-host-v2/proof.json');oracle_pins={}
for p,row in oracle['sources'].items():
 if ':' in p or p.startswith('/'):
  q=wp(p);assert sha(q)==row['sha256'];oracle_pins[p]=row['sha256']
 else:
  q=L/'wild-pool2-ee-producer-host-v2'/p; q=q if q.exists() else q.with_name(p+'.txt');assert sha(q)==row['sha256'];oracle_pins[p]=row['sha256']
for rel in ['game/src/gen/game_lighting.gen.cpp','game/inc/terrain_game.hpp','tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp']:
 key=str(L/'wild-pool2-ee-physical-v1'/rel);hit=[row for p,row in oracle['sources'].items() if wp(p)==Path(key)];assert len(hit)==1 and hit[0]['sha256']==review[rel],rel
result={'status':'PASS_INDEPENDENT_POOL2_EE_V2_POST_NATIVE_CONFIRMATION','sourceFiles':499,'sourceEqualsFinalReviewV1':True,'producerOracleV2SourceHashesBound':True,'rehashInputPins':len(pins),'dependencies':len(audit['dependencies']),'objects':len(audit['objectPins']),'sourceMirrors':len(audit['actualSourceMirrors']),'runtimeAssetsExactBaseline':298,'adpcm':adpcm,'elfSha256':sha(e),'symbolSha256':sha(s),'matchingTextSha256':th,'rootAuditSha256':sha(a.audit),'rootProvenanceSha256':sha(F/'root-native-provenance.json'),'checkerSha256':sha(Path(__file__)),'oracleProofSha256':sha(L/'wild-pool2-ee-producer-host-v2/proof.json'),'noBuildDeviceOrMirrorMutations':True,'limits':'Post-native file identity only; no physical benefit or target runtime output qualification.'}
a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
