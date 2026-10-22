from pathlib import Path
import importlib.util,json,hashlib,re
p=Path('F:/Projects/tyrax2-lab-20261001/night-ablation-controls-v2');sp=importlib.util.spec_from_file_location('night',p/'analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);text=(p/'host/O0-1-0-31-0-stdout.log').read_text();art=(p/'host/O0-1-0-31-0-artifact.log').read_text()
ptr=re.search(r'samplePtr=([0-9a-fA-F]{8})',text).group(1)
mutants={'wrongPhaseFirst':text.replace('first=1800','first=1801',1),'malformedSamplePointer':text.replace('samplePtr='+ptr,'samplePtr=xyz',1),'driftingSamplePointer':text.replace('samplePtr='+ptr,'samplePtr=00000001',1)};results=[]
for name,log in mutants.items():
 try:m.analyze(log,art,'host',1,0,31,0)
 except(ValueError,KeyError) as e:results.append({'guard':name,'rejected':True,'reason':str(e)})
 else:raise AssertionError(name)
f=p/'phase-pointer-controls.json';f.write_text(json.dumps({'status':'PASS_EXISTING_STRICT_PHASE_FIRST_AND_SHARED_POINTER_GUARDS','results':results,'newParserChanges':False},indent=2)+'\n');sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();r=json.loads((p/'proof.json').read_text());r['negativeParserGuards']=19;r['phasePointerSupplementSha256']=sha(f);r['originalProofSha256']=sha(p/'proof.json');r['artifactSha256'][str(f)]=sha(f);f=p/'final-proof-v2.json';assert not f.exists();f.write_text(json.dumps(r,indent=2)+'\n');print(sha(f))
