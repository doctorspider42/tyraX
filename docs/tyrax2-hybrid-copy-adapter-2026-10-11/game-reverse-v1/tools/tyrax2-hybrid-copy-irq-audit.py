"""Inspect linked A41 application IRQ closure, not BIOS routing or WCET."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--objdump',type=Path,required=True)
a=p.parse_args();root=a.fixture;sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
b=json.loads((root/'native-binding.json').read_text())
for rel,digest in b['files'].items():assert sha(root/rel)==digest,rel
trial=(root/'tyra/engine/inc/debug/tyrax2-hybrid-copy-trial.hpp').read_text()
assert 'Hardware():HybridCopyEE(HybridCopyTrial::queueProof,HybridCopyTrial::window){}' in trial
backend=(root/'tyra/engine/inc/debug/tyrax2-hybrid-copy-ee.hpp').read_text()
assert 'QueueProof queueProof;\n  WindowProof windowProof;' in backend
elf=root/'game/bin/vehicle-playground.elf.sym'
run=subprocess.run([str(a.objdump),'-dC',str(elf)],capture_output=True,text=True)
assert run.returncode==0,run.stderr
text=run.stdout;(root/'linked-disassembly.txt').write_text(text)
functions={m[1]:m[2] for m in re.finditer(r'^[0-9a-f]+ <([^\n]+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',text,re.M|re.S)}
roots=['Tyra::HybridCopyTrial::'+n+'(int)' for n in ('startISR','endISR','finishISR')]
callbacks=['Tyra::HybridCopyTrial::queueProof(unsigned long)','Tyra::HybridCopyTrial::window()']
todo=roots+callbacks;seen=set();calls=[];indirect=[]
while todo:
 name=todo.pop()
 if name in seen:continue
 seen.add(name);body=functions[name]
 assert '\tsyscall' not in body,name
 for line in body.splitlines():
  if '\tjalr\t' in line:indirect.append({'function':name,'instruction':line.strip()})
  if re.search(r'\t(jal|j|b)\t',line):
   target=re.search(r'<(.+)>',line)
   assert target,line
   target=re.sub(r'\+0x[0-9a-f]+$','',target[1])
   if target==name:continue
   calls.append({'from':name,'to':target});assert target in functions,target;todo.append(target)
assert not any(re.search(r'(DIntr|EIntr|GsGetIMR|GsPutIMR|SyncDCache|FlushCache|malloc|free|waitFor|flushRecording|advanceIfIdle|printf)',name) for name in seen)
# These are the two private, constructor-bound queue/window callbacks. This
# source pin and explicit callback closure is not a general indirect-call proof.
assert len(indirect)==2,indirect
assert all('blankStartISR' in r['function'] or r['function']==roots[0] for r in indirect),indirect
report={'valid':True,'elfSha256':b['elfSha256'],'symbolElfSha256':sha(elf),
 'disassemblySha256':sha(root/'linked-disassembly.txt'),'roots':roots,'boundCallbacks':callbacks,
 'reachable':sorted(seen),'calls':calls,'indirect':indirect,'trialSha256':sha(root/'tyra/engine/inc/debug/tyrax2-hybrid-copy-trial.hpp'),
 'limits':['Application closure plus source-bound callback targets, not BIOS interrupt routing.',
  'No elapsed-time, cache/lifetime, exclusive GS owner or WCET proof.',
  'Constructor/source binding is required; not arbitrary indirect target resolution.']}
(root/'irq-closure.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
