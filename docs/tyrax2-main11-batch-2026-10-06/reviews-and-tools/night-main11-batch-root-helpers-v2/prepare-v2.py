"""Isolated helper/parser clone; no fixture, production or runtime authority writes."""
from pathlib import Path
import shutil,json,hashlib
b=Path(__file__).parent.parent;h=Path(__file__).parent;c=b/'night-main11-batch-controls-v2';assert not c.exists()
oldh=b/'night-main11-batch-root-helpers-v1';oldc=b/'night-main11-batch-controls-v1'
for p in oldh.iterdir():
 if p.is_file()and p.suffix in('.py','.ps1','.md'):shutil.copy2(p,h/p.name)
c.mkdir()
for p in oldc.iterdir():
 if p.is_file()and p.suffix=='.py':shutil.copy2(p,c/p.name)
shutil.copytree(oldc/'source-controls',c/'source-controls')
for folder in(h,c):
 for p in folder.rglob('*'):
  if p.is_file()and p.suffix in('.py','.ps1','.md')and p.name!='prepare-v2.py':
   s=p.read_text(encoding='utf8')
   for label in('physical','root-helpers','controls'):s=s.replace('night-main11-batch-'+label+'-v1','night-main11-batch-'+label+'-v2')
   s=s.replace('29205','29206');p.write_text(s,encoding='utf8')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
authority=dict(status='DRAFT_KIND29_V2_SOURCE_AND_NATIVE_AUTHORITY_PENDING',draftUnreleased=True,pricingSourceManifestSha256=None,draftProducerManifestSha256=None,expectedFrozenSourceFiles=501,sourceSchemaDraft=True,candidateActivationSeparateFromCompletion=True,sourcePins={str(p):sha(p)for p in[c/'analyze-loop.py',c/'analyze-night.py',c/'analyze-night-cli.py',c/'corona_controls.py',c/'batch_controls.py',c/'source-controls/night_plan.hpp',c/'source-controls/stapip_vu1_shared_defines.h']})
(h/'host-authority.json').write_text(json.dumps(authority,indent=2)+'\n',encoding='utf8')
(h/'preparation.json').write_text(json.dumps(dict(status='DRAFT_KIND29_V2_UNRELEASED',sourceSchemaFinal=False,nativeAuthorityAvailable=False,physicalRuntimeAccepted=False,inheritedProofsCopied=False),indent=2)+'\n',encoding='utf8')
print('V2_NEW_PARSER_HELPERS_ONLY_NO_INHERITED_AUTHORITY')
