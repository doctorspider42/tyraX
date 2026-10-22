from pathlib import Path
import hashlib,json,argparse,subprocess
p=argparse.ArgumentParser();p.add_argument('--family',choices=('assert-gate','companion-census'),required=True);a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001');f=lab/(a.family+'-physical-v1');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
target=f/'target-source-manifest.json';draft=target if target.exists() else f/'draft-source-manifest.json'
m=json.loads(draft.read_text());assert not m['frozen'] and len(m['files'])==501
for rel,d in m['files'].items():assert sha(f/rel)==d,rel
base=lab/'corona24-pricing-physical-v1/target-source-manifest.json';bm=json.loads(base.read_text());assert set(m['files'])==set(bm['files'])
old=f/'root-draft-source-authority.json';assert not old.exists();old.write_bytes(draft.read_bytes())
frfile=f/'root-source-freeze.json';fr=json.loads(frfile.read_text()) if frfile.exists() else json.loads((f/'draft-review.json').read_text())
fr['draftBaseManifestSha256']=fr.get('baseManifestSha256');fr['draftManifestSha256']=sha(old)
fr['baseManifestSha256']=sha(base);fr['changes']=sorted(rel for rel,d in m['files'].items() if d!=bm['files'][rel]);fr['frozen']=True
fr['rootCodeHead']=subprocess.check_output(['git','rev-parse','HEAD'],cwd='F:/Projects/tyra-editor',text=True).strip()
fr['existingWaitsAndConsumerOwnershipRetained']=True;fr['noProductionPromotion']=True
if a.family=='assert-gate':
 proof=json.loads((lab/'assert-gate-source-controls-v1/root-proof.json').read_text());assert proof['validCases']==3 and proof['invalidFirstFailureCases']==12
 fr['hostOriginalExpressionControlsSha256']=sha(lab/'assert-gate-source-controls-v1/root-proof.json');fr['originalAssertionCount']=12
m['frozen']=True;m['status']='ROOT_FROZEN_PRIVATE_'+a.family.upper().replace('-','_')
target.write_bytes((json.dumps(m,indent=2)+'\n').encode());fr['sourceManifestSha256']=sha(target)
frfile.write_bytes((json.dumps(fr,indent=2)+'\n').encode())
print('ROOT_FROZEN',a.family,len(m['files']),len(fr['changes']),sha(target))
