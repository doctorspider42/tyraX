from pathlib import Path
import hashlib,json,subprocess,argparse
p=argparse.ArgumentParser();p.add_argument('--staged',action='store_true');a=p.parse_args()
repo=Path('F:/Projects/tyra-editor');root=repo/'docs/tyrax2-night-shift-2026-10-06'
sha=lambda b:hashlib.sha256(b).hexdigest()
manifest=json.loads((root/'payload-manifest.json').read_text())
for rel,digest in manifest['files'].items():assert sha((root/rel).read_bytes())==digest,rel
actual={str(f.relative_to(root)).replace('\\','/') for f in root.rglob('*') if f.is_file() and f.name!='payload-manifest.json'}
assert actual==set(manifest['files'])
for fixture in (root/'fixtures').iterdir():
 source=json.loads((fixture/'target-source-manifest.json').read_text());assert len(source['files'])==501
 for rel,digest in source['files'].items():assert sha((root/'source-blobs'/digest).read_bytes())==digest,(fixture.name,rel)
 native=json.loads((fixture/'root-native-provenance.json').read_text());assert native['target_source_manifest_sha256']==sha((fixture/'target-source-manifest.json').read_bytes())
for evidence in (root/'physical').iterdir():
 machine=json.loads((evidence/'machine-evidence.json').read_text());assert machine['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME'
 analysis=json.loads((evidence/'strict-analysis.json').read_text());assert analysis['engine_loops']==5400 and analysis['environment']=='ps2'
for evidence in (root/'emulator').iterdir():
 assert (evidence/'root-visual-review.json').exists()
 analysis=json.loads((evidence/'strict-analysis.json').read_text());assert analysis['engine_loops']==5400 and analysis['environment']=='emulator'
for name in ('tyra-engine-dev','tyra-testing'):
 expected=(repo/'.claude/skills'/name/'SKILL.md').read_bytes().replace(b'.claude/skills/',b'.agents/skills/').replace(b'CLAUDE.md',b'AGENTS.md')
 assert expected==(repo/'.agents/skills'/name/'SKILL.md').read_bytes()
if a.staged:
 paths=['docs/tyrax2-night-shift-2026-10-06/'+r for r in manifest['files']]+['docs/tyrax2-night-shift-2026-10-06/payload-manifest.json']
 command=['git','cat-file','--batch'];data=subprocess.check_output(command,input=''.join(':'+r+'\n' for r in paths).encode(),cwd=repo)
 pos=0
 for rel in paths:
  end=data.index(b'\n',pos);header=data[pos:end].split();assert header[1]==b'blob',(rel,header)
  size=int(header[2]);payload=data[end+1:end+1+size];assert sha(payload)==sha((repo/rel).read_bytes()),rel;pos=end+1+size+1
 assert pos==len(data)
print('PASS_NIGHT_SHIFT_ARCHIVE_SOURCE_RUNTIME_TWINS'+('_STAGED_BYTES' if a.staged else ''),len(manifest['files']))
