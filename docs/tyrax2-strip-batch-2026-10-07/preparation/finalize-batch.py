from pathlib import Path
import json,hashlib,ast,re,subprocess
repo=Path('F:/Projects/tyra-editor');dst=repo/'docs/tyrax2-strip-batch-2026-10-07';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();oldCounts={}
for name in ['empty-car','city-day-drive','city-physics','loop-balance','render-audit','queue-audit']:
 base=repo/'docs'/('tyrax2-'+name+'-2026-10-06');d=json.loads((base/'inventory.json').read_text(encoding='utf-8'));entries=d if isinstance(d,list)else d['files']
 entries=entries if isinstance(entries,list)else [dict(path=k,sha256=v)for k,v in entries.items()]
 for x in entries:assert sha(base/x['path'])==x['sha256'],(name,x['path'])
 oldCounts[name]=len(entries)
binding=json.loads((dst/'source-binding.json').read_text(encoding='utf-8'));native=json.loads((dst/'native-proof.json').read_text(encoding='utf-8'))
assert set(binding['unchanged'])|set(binding['changed'])==set(native['sources'])
for rel,x in binding['unchanged'].items():assert sha(repo/x['path'])==x['sha256']==native['sources'][rel]['sha256']
for rel,h in binding['changed'].items():assert sha(dst/'source'/rel)==h==native['sources'][rel]['sha256']
q=json.loads((dst/'physical/root-qualification.json').read_text(encoding='utf-8'));assert q['elfSha256']==native['elfSha256']and q['strictResultSha256']==sha(dst/'physical/strict-result.json')
for rel,key in [('native-proof.json','nativeProofSha256'),('root-review.json','rootReviewSha256'),('physical/launch-inputs.json','launchInputsSha256'),('physical/completed.stdout','rawPrefixSha256'),('physical/completed.log','normalizedLogSha256'),('physical/completed-samples.txt','sampleSha256')]:assert sha(dst/rel)==q[key],rel
assert sha(dst/'source-review.json')==native['sourceReviewSha256']
controls=json.loads((dst/'emulator/parser-controls.json').read_text(encoding='utf-8'));assert len(controls['cases'])==8 and all(x['rejected']for x in controls['cases'])
assert controls['parserSha256']==sha(dst/'preparation/analyze-batch.py')
for p in dst.rglob('*.py'):ast.parse(p.read_text(encoding='utf-8'))
for p in dst.rglob('*.md'):
 for link in re.findall(r'\]\(([^)]+)\)',p.read_text(encoding='utf-8')):
  if not link.startswith(('https:','http:','#')):assert (p.parent/link.split('#')[0]).exists(),(p,link)
assert 'tyrax2-strip-batch-2026-10-07/README.md'in(repo/'docs/README.md').read_text(encoding='utf-8')
cl=(repo/'.claude/skills/tyra-testing/SKILL.md').read_text(encoding='utf-8');ag=(repo/'.agents/skills/tyra-testing/SKILL.md').read_text(encoding='utf-8')
needle='Strip-packing evidence:';assert cl.split(needle)[1].split('Loop-balance diagnostics:')[0]==ag.split(needle)[1].split('Loop-balance diagnostics:')[0]
entries=[dict(path=p.relative_to(dst).as_posix(),sha256=sha(p),bytes=p.stat().st_size)for p in sorted(dst.rglob('*'))if p.is_file()and p.name!='inventory.json']
(dst/'inventory.json').write_text(json.dumps(dict(status='PASS_STRIP_BATCH_ARCHIVE_CLOSURE',parentArchivesVerified=oldCounts,files=entries),indent=2)+'\n',encoding='utf-8')
print('PASS_STRIP_BATCH_ARCHIVE_CLOSURE',len(entries),'oldPins',sum(oldCounts.values()))
