from pathlib import Path
import json,hashlib,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');old=lab/'wild-pool2-ee-physical-v1';new=lab/'wild-pool2-ee-physical-v2';base=lab/'wild-pool-table-physical-v3'
assert not new.exists();new.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=json.loads((old/'target-source-manifest.json').read_text())['files']
for rel,h in files.items():
 assert sha(old/rel)==h
 dst=new/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(old/rel,dst)
for folder in ('res','.res-baked'):shutil.copytree(base/'game'/folder,new/'game'/folder)
record={'status':'REJECT_V1_RUNTIME_ASSET_GATE_BEFORE_ANY_DEVICE_LAUNCH',
 'reason':'Native audio converter requires authored res/sfx input as well as baked WAVs; source-only restore lacked res.',
 'v1ElfSha256':sha(old/'game/bin/vehicle-playground.elf'),'v1NativeAuditSha256':sha(lab/'wild-pool2-ee-native-root-review-v1/proof.json'),
 'missingExpectedAssets':['sfx/engine_high.adpcm','sfx/engine_idle.adpcm','sfx/gear-shift.adpcm','sfx/tires_screech.adpcm'],
 'newSourceIdentical499':True,'v1DeviceLaunches':0,
 'authoredAssetInputs':{p.relative_to(new/'game/res').as_posix():sha(p)for p in(new/'game/res').rglob('*')if p.is_file()}}
(new/'root-asset-preparation.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
(lab/'pool2-ee-v1-rejected-asset-gate.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
helpers=lab/'wild-pool2-ee-root-v2';assert not helpers.exists();shutil.copytree(lab/'wild-pool2-ee-root-v1',helpers)
p=helpers/'prepare-physical.py';s=p.read_text();assert "fixture=lab/'wild-pool2-ee-physical-v1'" in s;s=s.replace("fixture=lab/'wild-pool2-ee-physical-v1'","fixture=lab/'wild-pool2-ee-physical-v2'");p.write_bytes(s.encode())
pins=helpers/'helper-pins.json';pins.write_bytes((json.dumps({'files':{p.name:sha(p)for p in helpers.iterdir()if p.is_file()and p.name!='helper-pins.json'},'source':'v1 helpers with physical fixture binding updated to v2 asset-complete copy'},indent=2)+'\n').encode())
print('V2: exact499 source, authored+baked asset trees, old attempted native evidence preserved')
