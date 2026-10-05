from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');fixture=lab/'corona24-pricing-physical-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
root=lab/'corona24-pricing-root-helpers-v1';assert not root.exists();shutil.copytree(lab/'wild-gs-sprite-corona-root-helpers-v2',root,ignore=shutil.ignore_patterns('__pycache__'))
for p in root.iterdir():
 if p.suffix not in ('.py','.ps1'):continue
 t=p.read_text(encoding='utf8').replace('wild-gs-sprite-corona-physical-v2','corona24-pricing-physical-v1').replace('wild-gs-sprite-corona-root-helpers-v2','corona24-pricing-root-helpers-v1').replace('==500','==501').replace('frozen500','frozen501').replace('Verified=500','Verified=501')
 p.write_bytes(t.encode())
host=root/'host-authority.json';h=json.loads(host.read_text());h['pricingSourceManifestSha256']=sha(fixture/'target-source-manifest.json');h['sourceFiles']=501;h['status']='ROOT_REBOUND_UNCHANGED_KIND9_SIX_CONTROLS_TO_CORONA24_PRICING';host.write_bytes((json.dumps(h,indent=2)+'\n').encode())
t=(lab/'audit-corona-native-v6-root-v1.py').read_text(encoding='utf8').replace("len(m['files'])==500","len(m['files'])==501").replace("F.name=='wild-gs-sprite-corona-physical-v2'","F.name=='corona24-pricing-physical-v1'").replace('len(mapping)==491','len(mapping)==492').replace("'sourceFiles':500","'sourceFiles':501").replace("'mirroredInputs':491","'mirroredInputs':492").replace('NATIVE_500','NATIVE_501')
old="reference=read(pin(F.parent/'wild-gs-sprite-corona-prototype-v6/target-source-manifest.json'));assert reference['files']==m['files'], 'exact V6 source inventory/bytes required'"
assert old in t
t=t.replace(old,"reference=read(pin(F.parent/'corona24-probe-physical-v1/target-source-manifest.json'));base=read(pin(F.parent/'wild-gs-sprite-corona-physical-v2/target-source-manifest.json'));expected=dict(base['files']);changes=fr['engineDeltas'];assert len(changes)==4\nfor name in changes:expected[name]=reference['files'][name]\nassert m['files']==dict(sorted(expected.items())), 'exact ordinary game and four qualified engine deltas required'")
t=t.replace('assembled=[]',"vsm=pin(Path(str(tc)+'.vsm'));assert len(re.findall(r'abs\\.[xyzw]+ VF00, VF18\\s+fmand',vsm.read_text()))==6\nqualified=read(pin(F.parent/'corona24-probe-native-root-review-v1/proof.json'));assert all(programs[k]['sha256']==qualified['actualVUPrograms'][k]['sha256'] for k in programs), 'all qualified diagnostic microprogram bytes retained'\nassembled=[]")
(lab/'audit-corona24-pricing-native-root-v1.py').write_bytes(t.encode())
t=(lab/'compile-corona-abi-v6-root-v1.py').read_text(encoding='utf8').replace('wild-gs-sprite-corona-physical-v2','corona24-pricing-physical-v1');(lab/'compile-corona24-pricing-abi-root-v1.py').write_bytes(t.encode())
for name in ('start','wait'):
 src=lab/f'{name}-lattice-physical-root-v1.ps1';t=src.read_text(encoding='utf8').replace('wild-pool-lattice-root-v2','corona24-pricing-root-helpers-v1').replace('--kind 8','--kind 9');(lab/f'{name}-corona24-physical-root-v1.ps1').write_bytes(t.encode())
review=root/'source-review.md';review.write_bytes(b'''# Private ordinary corona24 pricing source review\n\nThe ordinary game is exactly the old frozen V6 pricing source. Four engine deltas\nmatch the isolated qualified 24-bit candidate: renderer admission, three SUB\nscratch destinations, strict six paired ABS/FMAND dependencies, and TC-only\nrecipe/postprocessor. No observer header, pause or vblank waits enter pricing.\nAll normal scene effects and timing/presentation fences remain. Same ELF compares\nOff/On/Off and On/Off/On with unchanged six parser dependencies. Sparse EE counters\nare request witnesses only; isolated completed output and drawing pixels qualify\nthe candidate image, not acceptance for every ordinary quad or a hardware gain.\n''')
print(root)
