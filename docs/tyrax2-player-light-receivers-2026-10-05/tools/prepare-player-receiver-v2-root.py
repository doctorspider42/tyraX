from pathlib import Path
import json,hashlib,shutil
b=Path('F:/Projects/tyrax2-lab-20261001');src=b/'player-light-receivers-physical-v1';dst=b/'player-light-receivers-physical-v2';assert not dst.exists();m=json.loads((src/'target-source-manifest.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for n,h in m['files'].items():
 assert sha(src/n)==h;p=dst/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src/n,p)
for n in('res','.res-baked'):shutil.copytree(src/'game'/n,dst/'game'/n)
p=dst/'game/src/gen/game_vehicles.gen.cpp';s=p.read_text();a='void TerrainGame::renderVehicleGlass() {\n  for (int vi = 0; vi < vehicleCount_; ++vi) {\n    VehicleRt& v = vehicles_[vi];';assert s.count(a)==1;s=s.replace(a,a+'\n    NightAblation::ReceiverScope lightScope(experimentalPlayerReceiver(v.object));');p.write_bytes(s.encode())
p=dst/'game/src/gen/game_physics.gen.cpp';s=p.read_text();a='  for (int a = 0; a < hlCount; ++a) {\n    const int i = hlList[a];';assert s.count(a)==1;s=s.replace(a,a+'\n    NightAblation::ReceiverScope lightScope(experimentalPlayerReceiver(i));');p.write_bytes(s.encode())
for n in m['files']:m['files'][n]=sha(dst/n)
(dst/'target-source-manifest.json').write_bytes((json.dumps(m,indent=2)+'\n').encode());shutil.copy2(src/'receiver-experiment-design.json',dst/'receiver-experiment-design.json');(dst/'root-source-freeze.json').write_bytes((json.dumps(dict(sourceManifestSha256=sha(dst/'target-source-manifest.json'),sourceFiles=501,privateReceiverExperiment=True),indent=2)+'\n').encode())
for n in('root-helpers','controls'):
 target=b/('player-light-receivers-'+n+'-v2');shutil.copytree(b/('player-light-receivers-'+n+'-v1'),target)
 for p in target.rglob('*'):
  if p.is_file() and p.suffix in('.py','.ps1','.md','.json'):
   s=p.read_text().replace('player-light-receivers-physical-v1','player-light-receivers-physical-v2').replace('player-light-receivers-root-helpers-v1','player-light-receivers-root-helpers-v2').replace('player-light-receivers-controls-v1','player-light-receivers-controls-v2');p.write_bytes(s.encode())
h=b/'player-light-receivers-root-helpers-v2';c=b/'player-light-receivers-controls-v2';host=json.loads((h/'host-authority.json').read_text());host['sourcePins']={k:sha(Path(k))for k in host['sourcePins']};host['pricingSourceManifestSha256']=sha(dst/'target-source-manifest.json');(h/'host-authority.json').write_bytes((json.dumps(host,indent=2)+'\n').encode())
p=b/'audit-player-light-receivers-native-root-v2.py';p.write_bytes((b/'audit-player-light-receivers-native-root-v1.py').read_bytes().replace(b'player-light-receivers-physical-v1',b'player-light-receivers-physical-v2'))
print(dst,sha(dst/'target-source-manifest.json'))
