"""Test the actual queue ownership method, not an independently rewritten mock."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path);p.add_argument('--ee-cxx',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();assert not out.exists();out.mkdir(parents=True)
repo=Path(__file__).resolve().parents[1];sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
source=repo/'vendor/tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp'
text=source.read_text();start=text.index('bool Vif1Queue::canCopyCompletedNativeFrame(')
end=text.index('\nvoid Vif1Queue::waitFor(',start)
method=text[start:end];assert all(token not in method for token in ('flushRecording(', 'advanceIfIdle(', 'DIntr(', 'EIntr('))
assert 'submitted' not in method and 'nativePendingSequence' not in method
shutil.copy2(source,out/'actual-queue-source.cpp')
for name in ('verify-hybrid-copy-queue.cpp','verify-hybrid-copy-queue.py'):
    shutil.copy2(repo/'tools'/name,out/name)
template=(out/'verify-hybrid-copy-queue.cpp').read_text();assert template.count('// ACTUAL_QUEUE_METHOD')==1
generated=out/'queue-test.cpp';generated.write_text(template.replace('// ACTUAL_QUEUE_METHOD',method))
commands=[]
def run(label,args):
    commands.append(args);r=subprocess.run(args,text=True,capture_output=True)
    (out/(label+'.stdout.log')).write_text(r.stdout);(out/(label+'.stderr.log')).write_text(r.stderr)
    assert r.returncode==0,(label,r.stderr);return r.stdout
common=['-std=c++17','-Wall','-Wextra','-Werror']
results=[]
for mode,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    run(mode+'-compile',['g++',*common,*flags,str(generated),'-o',str(out/mode)])
    results.append(run(mode+'-run',[str(out/mode)]))
assert results[0]==results[1]=='HYBRIDQUEUE cases=16384 accepted=60 no-flush-no-advance=PASS\n'
run('ee-compile',[str(a.ee_cxx),*common,'-O2','-c',str(generated),'-o',str(out/'queue-ee.o')])
objdump=a.ee_cxx.with_name(a.ee_cxx.name.removesuffix('g++')+'objdump')
disassembly=run('ee-disassembly',[str(objdump),'-drC',str(out/'queue-ee.o')])
body=disassembly.split('<Vif1Queue::canCopyCompletedNativeFrame(unsigned long)>:',1)[1].split('\n\n',1)[0]
assert '\tlw\t' in body and '\tlbu\t' in body
assert not any('\t'+op+'\t' in body for op in ('jal','jalr','sb','sh','sw','sd','sq'))
assert sha(source)==sha(out/'actual-queue-source.cpp')
(out/'report.json').write_text(json.dumps(dict(valid=True,commands=commands,result=results[0],
    actualSourceSha256=sha(source),actualMethodSha256=hashlib.sha256(method.encode()).hexdigest(),
    limits=['Actual method under mocked volatile queue/MMIO state, not full engine execution.',
            'Caller must serialize ISR/main transitions.',
            'Does not prove GS FINISH/window/packet/framebuffer ownership.',
            'No actual ISR caller or performance change yet.']),indent=2)+'\n')
(out/'sha256.json').write_text(json.dumps({f.name:sha(f) for f in out.iterdir() if f.is_file()},indent=2)+'\n')
print(results[0],end='')
