from pathlib import Path
import argparse,hashlib,json,subprocess
p=argparse.ArgumentParser();p.add_argument('--staged',action='store_true');a=p.parse_args()
r=Path('F:/Projects/tyra-editor');root=r/'docs/tyrax2-claude-trials-2026-10-06';sha=lambda b:hashlib.sha256(b).hexdigest()
m=json.loads((root/'payload-manifest.json').read_text(encoding='utf8'))
actual={f.relative_to(root).as_posix() for f in root.rglob('*') if f.is_file() and f.name!='payload-manifest.json'};assert actual==set(m['files'])
for rel,h in m['files'].items():assert sha((root/rel).read_bytes())==h,rel
for fixture in (root/'fixtures').iterdir():
 s=json.loads((fixture/'target-source-manifest.json').read_text(encoding='utf8'));assert s['frozen'] and len(s['files'])==501
 for rel,h in s['files'].items():assert sha((root/'source-blobs'/h).read_bytes())==h,(fixture.name,rel)
 n=json.loads((fixture/'root-native-provenance.json').read_text(encoding='utf8'));assert n['build_exit_code']==0 and n['verified_source_files']==501 and n['runtime_assets']==298 and n['target_source_manifest_sha256']==sha((fixture/'target-source-manifest.json').read_bytes())
for fixture in (root/'unreleased-drafts').iterdir():
 manifest=fixture/'target-source-manifest.json'
 if not manifest.exists():continue
 s=json.loads(manifest.read_text(encoding='utf8'));assert not s['frozen']and len(s['files'])==501
 for rel,h in s['files'].items():assert sha((root/'source-blobs'/h).read_bytes())==h,(fixture.name,rel)
 assert not (fixture/'root-runtime-authority.json').exists()
for environment in ('physical','emulator'):
 for evidence in (root/environment).iterdir():
  q=json.loads((evidence/'strict-analysis.json').read_text(encoding='utf8'));assert q['engine_loops']==5400 and q['environment']==('ps2' if environment=='physical' else 'emulator')
  assert json.loads((evidence/'machine-evidence.json').read_text(encoding='utf8'))['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME'
assert len(list((root/'physical').iterdir()))==2 and len(list((root/'emulator').iterdir()))==3
for name in ('tyra-engine-dev','tyra-testing'):
 source=(r/'.claude/skills'/name/'SKILL.md').read_bytes();assert source.replace(b'.claude/skills/',b'.agents/skills/').replace(b'CLAUDE.md',b'AGENTS.md')==(r/'.agents/skills'/name/'SKILL.md').read_bytes()
if a.staged:
 paths=['docs/tyrax2-claude-trials-2026-10-06/'+x for x in m['files']]+['docs/tyrax2-claude-trials-2026-10-06/payload-manifest.json']
 data=subprocess.check_output(['git','cat-file','--batch'],input=''.join(':'+x+'\n' for x in paths).encode(),cwd=r);pos=0
 for rel in paths:
  end=data.index(b'\n',pos);header=data[pos:end].split();assert header[1]==b'blob',(rel,header);length=int(header[2]);payload=data[end+1:end+1+length];assert sha(payload)==sha((r/rel).read_bytes()),rel;pos=end+1+length+1
 assert pos==len(data)
print('PASS_CLAUDE_TRIALS_ARCHIVE_SOURCE_RUNTIME_TWINS'+('_STAGED_BYTES' if a.staged else ''),len(m['files']))
