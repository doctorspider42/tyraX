"""Test the actual generated A33 cold-head parser without fabricating target runs."""
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
reader=Path(__file__).with_name('tyrax2-object-head-analyze.py')
text=reader.read_text();finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(reader),'__name__':'object_head_reader_test'}
exec(compile(text.rstrip()[:-len(finish)],str(reader),'exec'),scope)
generated=scope['source'];tree=ast.parse(generated)
node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='heads')
function=ast.get_source_segment(generated,node)
context={'re':re,'count':6}
exec(compile(function,str(reader),'exec'),context)
reference='\n'.join(f'OBJECTHEAD phase={phase} arm={arm} checks=27 draws={27 if arm==1 else 0} words=338 hash={phase+1:08x}'
                    for phase in range(6) for arm in range(5))
cases={'reference':reference,'missing':reference.replace('OBJECTHEAD phase=0 arm=1','MISSINGOBJECTHEAD phase=0 arm=1'),
       'duplicate':reference+'\n'+reference.splitlines()[0]}
for field,value in (('checks','0'),('draws','0'),('words','0'),('hash','00000000'),('phase','7'),('arm','2')):
    cases[field]=re.sub(r'(OBJECTHEAD phase=0 arm=1[^\n]* '+field+r'=)[^\s]+',r'\g<1>'+value,reference) if field not in ('phase','arm') else reference.replace('phase=0 arm=1',f'phase={value if field=="phase" else 0} arm={value if field=="arm" else 1}',1)
cases['control-draw']=reference.replace('phase=0 arm=0 checks=27 draws=0','phase=0 arm=0 checks=27 draws=1',1)
cases['changed-digest']=reference.replace('phase=0 arm=1 checks=27 draws=27 words=338 hash=00000001','phase=0 arm=1 checks=27 draws=27 words=338 hash=00000002',1)
results={}
for name,log in cases.items():
    try:context['heads'](log)
    except AssertionError:results[name]=False
    else:results[name]=True
assert results=={name:name=='reference' for name in cases},results
assert "report['performanceCandidate']=False" in generated
assert "report.pop('contrasts',None);report.pop('firstOrderGate',None)" in generated
(a.out/'generated-head-parser.py').write_text(function+'\n')
(a.out/'reader-controls.json').write_text(json.dumps({'readerSha256':hashlib.sha256(text.encode()).hexdigest(),
    'cases':results,'scope':'actual generated cold-head parser only; no target/FPS proof'},indent=2)+'\n')
print('PASS actual head reader:1 positive/'+str(len(results)-1)+' rejecting controls; pricing flags removed')
