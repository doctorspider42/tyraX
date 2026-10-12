"""Exercise actual generated A35 preflight parsers, not target performance."""
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path)
a=p.parse_args();assert not a.out.exists();a.out.mkdir(parents=True)
reader=Path(__file__).with_name('tyrax2-world-front-preflight.py')
text=reader.read_text();finish="exec(compile(source,str(__file__),'exec'))"
assert text.rstrip().endswith(finish)
scope={'__file__':str(reader),'__name__':'world_front_reader_test'}
exec(compile(text.rstrip()[:-len(finish)],str(reader),'exec'),scope)
generated=scope['source'];tree=ast.parse(generated)
functions='\n\n'.join(ast.get_source_segment(generated,n) for n in tree.body
 if isinstance(n,ast.FunctionDef) and n.name in ('frontWitness','frontOracle','movingOwner','frontAdmission'))
phases=[{'mode':7 if i%3==1 else 0,'witnessFrames':4} for i in range(6)]
ctx={'re':re,'count':6,'phases':phases};exec(compile(functions,str(reader),'exec'),ctx)
reference='\n'.join(
 f"FRONTWITNESS phase={i} mode={phase['mode']} frames=4 inside={6 if phase['mode']==7 else 0} partial={7 if phase['mode']==7 else 0} outside=0 live={72 if phase['mode']==7 else 0} peak=73\n"
 f"WORLDWITNESS phase={i} mode={phase['mode']} frames=4 draws=17 qwords=17420\n"+
 f"FRONTADMISSION phase={i} checks=44 headUnchanged=1\n"+
 '\n'.join(f'FRONTORACLE phase={i} arm={arm} inside={6 if arm==1 else 0} partial={7 if arm==1 else 0} outside=0 heads=28 words=354 hash={i+1:08x}' for arm in range(5))
 for i,phase in enumerate(phases))
cases={'reference':reference}
for prefix in ('FRONTWITNESS','FRONTORACLE','WORLDWITNESS','FRONTADMISSION'):
 cases['missing-'+prefix]=reference.replace(prefix+' phase=1','MISSING'+prefix+' phase=1')
 cases['duplicate-'+prefix]=reference+'\n'+next(l for l in reference.splitlines() if l.startswith(prefix))
for field,value in (('mode','0'),('frames','0'),('inside','0'),('partial','0'),('live','0'),('peak','1025')):
 cases['moving-'+field]=re.sub(r'(FRONTWITNESS phase=1[^\n]* '+field+r'=)\d+',r'\g<1>'+value,reference)
for field in ('inside','partial','outside','live'):
 cases['baseline-'+field]=re.sub(r'(FRONTWITNESS phase=0[^\n]* '+field+r'=)\d+',r'\g<1>1',reference)
for field,value in (('inside','0'),('partial','0'),('heads','0'),('words','1'),('hash','000000ff')):
 cases['oracle-'+field]=re.sub(r'(FRONTORACLE phase=0 arm=1[^\n]* '+field+r'=)[0-9a-f]+',r'\g<1>'+value,reference)
cases['common-body-inactive']=reference.replace('WORLDWITNESS phase=0 mode=0 frames=4 draws=17','WORLDWITNESS phase=0 mode=0 frames=4 draws=0')
cases['admission-head']=reference.replace('headUnchanged=1','headUnchanged=0')
cases['admission-checks']=reference.replace('checks=44','checks=38')
cases['admission-phase']=reference.replace('FRONTADMISSION phase=1','FRONTADMISSION phase=0')
results={}
for name,log in cases.items():
 try:
  for fn in ('frontWitness','frontOracle','movingOwner','frontAdmission'):ctx[fn](log)
 except AssertionError:results[name]=False
 else:results[name]=True
assert results=={name:name=='reference' for name in cases},results
assert "report['performanceCandidate']=True" not in generated
assert "report['firstOrderGate']=" not in generated
assert "report['worldFrontStage']='early-Core-preflight-only'" in generated
(a.out/'generated-parsers.py').write_text(functions+'\n')
(a.out/'reader-controls.json').write_text(json.dumps({'readerSha256':hashlib.sha256(text.encode()).hexdigest(),
 'cases':results,'scope':'actual generated parsers, no target/FPS proof'},indent=2)+'\n')
print(f'PASS actual A35 preflight reader:1 positive/{len(results)-1} rejecting controls')
