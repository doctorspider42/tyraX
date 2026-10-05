"""Root-only preflight and immutable physical launch inputs, no device command."""
from pathlib import Path
import argparse, hashlib, json, shutil
p=argparse.ArgumentParser();p.add_argument('--stem',required=True);p.add_argument('--kind',type=int,choices=(8,),required=True);p.add_argument('--order',type=int,choices=(0,1),required=True);p.add_argument('--restored',type=int,default=0);a=p.parse_args()
assert a.stem.startswith('night-ablation-ps2-') and all(c.isalnum() or c=='-' for c in a.stem)
lab=Path('F:/Projects/tyrax2-lab-20261001');fixture=lab/'wild-pool-lattice-physical-v7';game=fixture/'game/bin';archive=lab/(a.stem+'-launch');assert not archive.exists()
assert not (game/'night-ablation.log').exists(),'previous artifact must be archived first'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
manifest=json.loads((fixture/'target-source-manifest.json').read_text());assert manifest['frozen']
for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
native=json.loads((fixture/'root-native-provenance.json').read_text());assert native['selected_elf_sha256']==sha(game/'vehicle-playground.elf');assert native['target_source_manifest_sha256']==sha(fixture/'target-source-manifest.json')
for rel,h in json.loads((fixture/'runtime-assets-manifest.json').read_text())['files'].items():assert sha(game/rel)==h,rel
joint=0; assert a.kind==8 and a.restored==0
assert a.kind==8 and a.restored==0
(game/'night-ablation.cfg').write_bytes(f'{a.kind} {a.order} {joint} {a.restored}\n'.encode());(game/'quiet-night.cfg').write_bytes(b'1\n');(game/'ps2link.run').write_bytes(b'ps2link')
archive.mkdir()
for f in ('night-ablation.cfg','quiet-night.cfg','ps2link.run','vehicle-playground.elf','vehicle-playground.elf.sym'):shutil.copy2(game/f,archive/f)
for f in ('target-source-manifest.json','root-source-freeze.json','root-native-provenance.json','runtime-assets-manifest.json','root-runtime-authority.json'):shutil.copy2(fixture/f,archive/f)
record=dict(stem=a.stem,fixture=str(fixture),kind=a.kind,order=a.order,joint=joint,restored=a.restored,elfSha256=sha(game/'vehicle-playground.elf'),launchArchive=str(archive),helperSha256=sha(Path(__file__)),launchFiles={f.name:sha(f)for f in archive.iterdir()if f.is_file()},physicalStartAccepted=False)
(archive/'owned-launch.json').write_text(json.dumps(record,indent=2)+'\n');print(archive)
