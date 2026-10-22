from pathlib import Path
import hashlib,json,re,subprocess
repo=Path('F:/Projects/tyra-editor');out=Path(__file__).parent;engine=repo/'vendor/tyra/engine'
tracked=subprocess.check_output(['git','ls-files','vendor/tyra'],cwd=repo,text=True).splitlines();seeds=[n for n in tracked if Path(n).suffix in ('.vclpp','.vcl','.vsm')];todo=seeds.copy();seen={};unresolved=[]
sha=lambda b:hashlib.sha256(b).hexdigest()
while todo:
 n=todo.pop()
 if n in seen:continue
 p=repo/n;b=p.read_bytes();head=subprocess.check_output(['git','show','HEAD:'+n],cwd=repo);assert b.replace(b'\r\n',b'\n')==head.replace(b'\r\n',b'\n'),n
 seen[n]=dict(actualSha256=sha(b),headBlobSha256=sha(head),normalizedTextEqualHead=True)
 for include in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]',b.decode(encoding='utf8'),re.M):
  candidates=[engine/include,p.parent/include,engine/'inc'/include]
  match=next((c for c in candidates if c.is_file()),None)
  if match is None:unresolved.append(dict(source=n,include=include));continue
  rel=match.resolve().relative_to(repo.resolve()).as_posix();assert rel in tracked,rel;todo.append(rel)
diff=subprocess.check_output(['git','diff','--name-only','HEAD','--',*seen],cwd=repo,text=True);assert not diff.strip();assert not unresolved,unresolved
proof=json.loads((out/'proof.json').read_text(encoding='utf8'));budgets={k:v['roundedMicroInstructions']for k,v in proof['actualVUPrograms'].items()};record=dict(status='PASS_TRACKED_VU_AND_RECURSIVE_SOURCE_INCLUDE_CLOSURE_EQUAL_HEAD',head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),rootSources=len(seeds),closureFiles=len(seen),files=seen,unresolvedIncludes=unresolved,currentLinkedMicroInstructionBudgets=budgets,currentProgramMax=max(budgets.values()),currentAll16Compared=True,baselineIdenticalPrograms=sum(v['identical']for v in proof['actualVUPrograms'].values()),expectedPrivateCoronaSpriteDifference='StaPipVU1Cull_TC_CodeStart',note='Archived main11 TC contains a private corona SPRITE prototype; current production TC equals HEAD source. No public receiver edit changes VU source. Microinstruction counts are image sizes, not resident simultaneous placement or FPS.',layoutAndImagesProofSha256=sha((out/'proof.json').read_bytes()))
(out/'head-vu-closure-proof.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf8');print(record['status'],len(seeds),len(seen),max(budgets.values()))
