"""Extract actual A40 Hybrid emitter into isolated full-resolution copy trial."""
import argparse, hashlib, importlib.util, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('parent',type=Path);p.add_argument('out',type=Path)
p.add_argument('--toolchain',type=Path,required=True)
a=p.parse_args();root=a.out.resolve();assert not root.exists();root.mkdir(parents=True)
parent=a.parent.resolve();repo=Path(__file__).resolve().parent
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
binding=json.loads((parent/'native-binding.json').read_text())
rel='tyra/engine/src/renderer/core/gs/renderer_core_gs.cpp'
assert sha(parent/rel)==binding['files'][rel]
text=(parent/rel).read_text()
shutil.copy2(parent/rel,root/'parent-gs.cpp')
spec=importlib.util.spec_from_file_location('quiet',repo/'tyrax2-quiet-fixture.py')
q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
methods=[]
for signature in ('RendererCoreGS::RasterTarget RendererCoreGS::getRasterTarget() const',
                  'qword_t* RendererCoreGS::emitRasterRestore(qword_t* q, bool texFlush)',
                  'void RendererCoreGS::emitHybridPresent(u8 target)'):
    start=text.index(signature);body=text.index('{',start);depth=0;end=None
    masked=q.re.sub(r'//[^\n]*|/\*[\s\S]*?\*/',lambda m:' '*len(m[0]),text)
    for i in range(body,len(masked)):
        depth+=(masked[i]=='{')-(masked[i]=='}')
        if depth==0:end=i+1;break
    assert end
    methods.append(text[start:end])
template=(repo/'tyrax2-copy-budget.cpp').read_text()
assert template.count('// ACTUAL_METHODS')==1
generated=root/'copy.cpp';generated.write_text(template.replace('// ACTUAL_METHODS','\n\n'.join(methods)))
for name in ('tyrax2-copy-budget.cpp','tyrax2-copy-budget.py'):shutil.copy2(repo/name,root/name)
bin=root/'game/bin';bin.mkdir(parents=True)
tc=a.toolchain.resolve();sdk=tc/'ps2sdk';cxx=tc/'ee/bin/mips64r5900el-ps2-elf-g++'
args=[str(cxx),'-std=c++17','-D_EE','-G0','-O2','-Wall','-Wextra','-Werror',
      '-I'+str(sdk/'ee/include'),'-I'+str(sdk/'common/include'),'-T'+str(sdk/'ee/startup/linkfile'),
      '-L'+str(sdk/'ee/lib'),'-Wl,-zmax-page-size=128',str(generated),'-lgraph','-ldraw',
      '-lpacket2','-ldebug','-ldma','-o',str(bin/'vehicle-playground.elf')]
env=os.environ.copy();env.update(PS2DEV=str(tc),PS2SDK=str(sdk))
r=subprocess.run(args,env=env,text=True,capture_output=True)
(root/'build.stdout.log').write_text(r.stdout);(root/'build.stderr.log').write_text(r.stderr)
assert r.returncode==0,r.stderr
(root/'build.json').write_text(json.dumps(dict(command=args,parentElf=binding['elfSha256'],
    parentSourceSha256=sha(root/'parent-gs.cpp'),methodsSha256=[hashlib.sha256(m.encode()).hexdigest() for m in methods],
    files={str(f.relative_to(root)):sha(f) for f in root.rglob('*') if f.is_file()},
    limits=['Isolated copy trial, no game or in-game timer ownership.',
            'Original method bodies unchanged; dispatch stubs build only.',
            'Appended SDK FINISH changes synchronization versus stock Hybrid2.']),indent=2)+'\n')
print(json.dumps(dict(elfSha256=sha(bin/'vehicle-playground.elf'),out=str(root))))
