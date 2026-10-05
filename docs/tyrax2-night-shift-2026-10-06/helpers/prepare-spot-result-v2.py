from pathlib import Path
import shutil,json,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');base=b/'spot-result-physical-v1';out=b/'spot-result-physical-v2';assert not out.exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((base/'target-source-manifest.json').read_text())
for n,d in m['files'].items():
 assert sha(base/n)==d;p=out/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/n,p)
for n in('res','.res-baked'):shutil.copytree(base/'game'/n,out/'game'/n)
n='tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';p=out/n;s=p.read_text();old='static unsigned spotResultDifference(const StaPipClipperSpot& a,const StaPipClipperSpot& b){';assert s.count(old)==1;s=s.replace(old,old+'\n  if(!a.enabled||!b.enabled)return unsigned(a.enabled!=b.enabled); // No inactive uninitialized Vec4 reads.');s=s.replace('NightAblation::producerCount(0,3,15);','NightAblation::producerCount(0,3,reference.enabled?15:1);\n    NightAblation::producerCount(1,0,reference.enabled?1:0);\n    NightAblation::producerCount(1,1,reference.enabled&&!eligible?1:0);');p.write_bytes(s.encode())
for n in m['files']:m['files'][n]=sha(out/n)
(out/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());fr=json.loads((base/'root-source-freeze.json').read_text());fr['sourceManifestSha256']=sha(out/'target-source-manifest.json');fr['candidate']+=' V2 comparator reads15 initialized active outputs or only enabled for inactive; counts active/nonfinite fallbacks separately. V1 inactive-vector oracle rejected.';(out/'root-source-freeze.json').write_bytes((json.dumps(fr,indent=2)+'\n').encode())
for old,new in [('spot-result-build-native-v1.ps1','spot-result-build-native-v2.ps1'),('compile-spot-result-abi-root-v1.py','compile-spot-result-abi-root-v2.py'),('audit-spot-result-native-root-v1.py','audit-spot-result-native-root-v2.py'),('test-spot-result-source-v1.py','test-spot-result-source-v2.py')]:
 s=(b/old).read_text().replace('spot-result-physical-v1','spot-result-physical-v2').replace('night-ablation-native-v50','night-ablation-native-v51').replace('spot-result-source-controls-v1','spot-result-source-controls-v2')
 if old.startswith('test-'):s=s.replace('struct Vec4 {float x=0,y=0,z=0,w=1;};','struct Vec4 {float x,y,z,w;Vec4(){};};')
 (b/new).write_bytes(s.encode())
print('V2 inactive-field oracle fixed; candidate unchanged')
