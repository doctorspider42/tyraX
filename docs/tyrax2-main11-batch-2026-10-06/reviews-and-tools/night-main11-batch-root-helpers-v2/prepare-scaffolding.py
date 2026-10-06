from pathlib import Path
import shutil,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');h=Path(__file__).parent;c=b/'night-main11-batch-controls-v2';assert not c.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
shutil.copytree(b/'tex1-owned-controls-v1',c,ignore=shutil.ignore_patterns('__pycache__','synthetic','host-*','*.json','*.log','run-controls.py','README.md'))
shutil.copytree(b/'tex1-owned-root-helpers-v1',h,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','synthetic','host-controls-proof.json','helper-pins.json','host-authority.json','preparation.json','host-kind9-*'))
for folder in (h,c):
 for p in folder.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.json','.md')and p.name!='prepare-scaffolding.py':
   s=p.read_text(encoding='utf8').replace('tex1-owned','night-main11-batch');
   if p.parent==h or p.name=='analyze-night-cli.py':s=re.sub(r'\b28\b','29',s)
   p.write_bytes(s.encode())
p=c/'source-controls/night_plan.hpp';p.write_bytes(p.read_text(encoding='utf8').replace('kind==28','kind==29').encode())
p=c/'analyze-night.py';s=p.read_text(encoding='utf8').replace(',23,28)',',23,28,29)');start=s.index('  if kind==28:');end=s.index('  r.update(producerObserverFlags',start);s=s[:start]+'''  if kind==29:
   raise ValueError('kind29 cold geometry/carrier source schema still pending independent review')
'''+s[end:];p.write_bytes(s.encode())
a=dict(status='DRAFT_KIND29_SOURCE_AND_NATIVE_AUTHORITY_PENDING',draftUnreleased=True,sourcePins={str(p):sha(p)for p in [c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']},pricingSourceManifestSha256=None,expectedFrozenSourceFiles=501);(h/'host-authority.json').write_bytes((json.dumps(a,indent=2)+'\n').encode())
review='''# Kind29 main-only night-box batching draft

Source and native authority are pending. No runtime release exists here. The parser currently fails closed until the exact cold source schema and member/carrier identity witnesses are reviewed. Root owns source freeze, actual native audit and devices.

One ELF, full ordinary night/tableOn in both arms, Off/On/Off and reverse,5400 loops, existing loop sampler and cold750/1155 only. No new Count reads or waits. Main-only candidate must demonstrate actual grouped replacements, exact geometry oracle counters without mismatches, actual original-vs-merged eligibility, zero duplicate main submissions and preserved reflection carriers/content/cadence. Aggregate group counts alone do not qualify packet/VU output or reflection equivalence. Fallback/demotion reasons remain explicit. Common copied geometry/cache/layout remains unpriced against production without this candidate.

TEX1/companion runtime guards retain501 source, native proof/provenance/source/ELF/symbol identity,298 assets and strict complete loop capture. Host malformed controls prove parser rejection only. No physical timing, semantic runtime acceptance or production promotion follows from the scaffolding.
''';(h/'source-review.md').write_bytes(review.encode());(h/'README.md').write_bytes(review.encode());(h/'preparation.json').write_bytes((json.dumps(dict(status='DRAFT_KIND29_UNRELEASED',sourceSchemaFinal=False,nativeAuthorityAvailable=False,physicalRuntimeAccepted=False),indent=2)+'\n').encode());(h/'helper-pins.json').write_bytes((json.dumps({p.name:sha(p)for p in h.iterdir()if p.is_file()and p.name!='helper-pins.json'},indent=2)+'\n').encode());print('DRAFT_KIND29_FAIL_CLOSED_SCAFFOLD_CREATED')
