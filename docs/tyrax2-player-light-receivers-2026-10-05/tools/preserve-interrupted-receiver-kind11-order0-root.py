from pathlib import Path
import json,hashlib,shutil
b=Path('F:/Projects/tyrax2-lab-20261001');stem='night-ablation-ps2-player-receivers-kind11-order0-20261005';out=b/(stem+'-interrupted');assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();launch=b/(stem+'-launch');d=json.loads((launch/'owned-launch.json').read_text(encoding='utf-8-sig'));f=Path(d['fixture']);artifact=f/'game/bin/night-ablation.log';raw=b/(stem+'.log')
for p in launch.iterdir():
 if p.is_file()and p.suffix not in('.elf','.sym'):shutil.copy2(p,out/p.name)
for p,n in((raw,'ps2client-raw.log'),(artifact,'night-ablation-partial.log')):
 assert p.exists();shutil.copy2(p,out/n);assert sha(p)==sha(out/n)
text=raw.read_bytes().decode('latin1');assert 'NIGHTDONE ' not in text
(out/'interruption.json').write_bytes((json.dumps(dict(status='INCOMPLETE_HOST_INTERRUPTED_NO_COMPLETION_ACCEPTED',reason='User reported Codex crash; exact ps2client process absent on recovery',physicalHangEstablished=False,pingResponds=True,sourceManifestSha256=sha(f/'target-source-manifest.json'),elfSha256=d['elfSha256'],rawSha256=sha(raw),artifactSha256=sha(artifact),files={p.name:sha(p)for p in out.iterdir()if p.is_file()}),indent=2)+'\n').encode())
# Only remove the exact archived partial host artifact so a fresh boot gets a clean output file.
assert sha(artifact)==sha(out/'night-ablation-partial.log');artifact.unlink();print(out)
