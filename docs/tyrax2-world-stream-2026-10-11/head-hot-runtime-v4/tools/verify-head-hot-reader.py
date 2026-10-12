"""Reject mutations using the actual generated A34 acceptance functions."""
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
reader=Path(__file__).with_name('tyrax2-head-hot-analyze.py')
text=reader.read_text();finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(reader),'__name__':'head_hot_reader_test'}
exec(compile(text.rstrip()[:-len(finish)],str(reader),'exec'),scope)
generated=scope['source'];tree=ast.parse(generated)
functions='\n\n'.join(ast.get_source_segment(generated,n) for n in tree.body
 if isinstance(n,ast.FunctionDef) and n.name in ('heads','movingHeads','movingOwner'))
phases=[{'mode':7 if n%3==1 else 0,'witnessFrames':4} for n in range(6)]
ctx={'re':re,'count':6,'phases':phases}
exec(compile(functions,str(reader),'exec'),ctx)
reference='\n'.join(
 f"HEADWITNESS phase={i} mode={p['mode']} frames=4 draws={25 if p['mode']==7 else 0} qwords={325 if p['mode']==7 else 0} compiles={1 if p['mode']==7 else 0} live={100 if p['mode']==7 else 0} peak=200\n"
 f"WORLDWITNESS phase={i} mode={p['mode']} frames=4 draws=17 qwords=17420\n"+
 '\n'.join(f'OBJECTHEAD phase={i} arm={arm} checks=27 draws={27 if arm==1 else 0} words=338 hash={i+1:08x}' for arm in range(5))
 for i,p in enumerate(phases))
cases={'reference':reference}
for prefix in ('HEADWITNESS','WORLDWITNESS','OBJECTHEAD'):
 cases['missing-'+prefix]=reference.replace(prefix+' phase=1','MISSING'+prefix+' phase=1')
 cases['duplicate-'+prefix]=reference+'\n'+next(l for l in reference.splitlines() if l.startswith(prefix))
for field,value in (('mode','0'),('frames','0'),('draws','0'),('qwords','0'),('live','0'),('peak','32769')):
 cases['moving-head-'+field]=re.sub(r'(HEADWITNESS phase=1[^\n]* '+field+r'=)\d+',r'\g<1>'+value,reference)
for field in ('draws','qwords','compiles','live'):
 cases['baseline-head-'+field]=re.sub(r'(HEADWITNESS phase=0[^\n]* '+field+r'=)\d+',r'\g<1>1',reference)
cases['common-world-missing']=reference.replace('WORLDWITNESS phase=0 mode=0 frames=4 draws=17','WORLDWITNESS phase=0 mode=0 frames=4 draws=0')
cases['head-digest']=reference.replace('arm=1 checks=27 draws=27 words=338 hash=00000001','arm=1 checks=27 draws=27 words=338 hash=00000002')
results={}
for name,log in cases.items():
 try:
  for fn in ('heads','movingHeads','movingOwner'):ctx[fn](log)
 except AssertionError:results[name]=False
 else:results[name]=True
assert results=={name:name=='reference' for name in cases},results
assert "report['admittedHotOriginalHead']=False" in generated
assert "report['commonWorldHot']=True" in generated
assert "report['performanceCandidate']=True" in generated
(a.out/'generated-parsers.py').write_text(functions+'\n')
(a.out/'reader-controls.json').write_text(json.dumps({'readerSha256':hashlib.sha256(text.encode()).hexdigest(),
 'cases':results,'scope':'generated acceptance functions only; no target/FPS proof'},indent=2)+'\n')
print('PASS actual hot head reader:1 positive/'+str(len(results)-1)+' rejecting controls')
